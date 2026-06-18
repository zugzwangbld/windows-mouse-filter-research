#include "moufiltr.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, MouFilter_EvtDeviceAdd)
#pragma alloc_text (PAGE, MouFilter_EvtIoInternalDeviceControl)
#endif

#pragma warning(push)
#pragma warning(disable:4055)
#pragma warning(disable:4152)

static __forceinline LONGLONG
MsToQpcTicks(PDEVICE_EXTENSION DevExt, LONG Ms)
{
    return (DevExt->QpcFrequency.QuadPart * Ms) / 1000;
}

static __forceinline LONGLONG
UsToQpcTicks(PDEVICE_EXTENSION DevExt, LONG Us)
{
    return (DevExt->QpcFrequency.QuadPart * Us) / 1000000;
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, MouFilter_EvtDeviceAdd);

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );
}

NTSTATUS
MouFilter_EvtDeviceAdd(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDFDEVICE hDevice;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_EXTENSION);
    deviceAttributes.EvtCleanupCallback = MouFilter_EvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    {
        PDEVICE_EXTENSION devExt = FilterGetData(hDevice);

        RtlZeroMemory(devExt, sizeof(DEVICE_EXTENSION));

        KeInitializeSpinLock(&devExt->WheelLock);
        InitializeListHead(&devExt->WheelQueue);

        devExt->WheelQueueCount = 0;
        devExt->WheelTimerRunning = FALSE;
        devExt->NextReleaseQpc.QuadPart = 0;
        devExt->LastReleaseQpc.QuadPart = 0;

        KeQueryPerformanceCounter(&devExt->QpcFrequency);

        KeInitializeTimerEx(&devExt->WheelTimer, NotificationTimer);

        KeInitializeDpc(
            &devExt->WheelDpc,
            MouFilter_WheelDpcFunc,
            devExt
        );
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &ioQueueConfig,
        WdfIoQueueDispatchParallel
    );

    ioQueueConfig.EvtIoInternalDeviceControl =
        MouFilter_EvtIoInternalDeviceControl;

    return WdfIoQueueCreate(
        hDevice,
        &ioQueueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
    );
}

VOID
MouFilter_DispatchPassThrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
)
{
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status;

    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET
    );

    ret = WdfRequestSend(Request, Target, &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

VOID
MouFilter_EvtIoInternalDeviceControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
)
{
    PDEVICE_EXTENSION devExt;
    PCONNECT_DATA connectData;
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE hDevice;
    size_t length;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch (IoControlCode)
    {
    case IOCTL_INTERNAL_MOUSE_CONNECT:

        if (devExt->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(CONNECT_DATA),
            &connectData,
            &length
        );

        if (!NT_SUCCESS(status)) {
            break;
        }

        devExt->UpperConnectData = *connectData;

        connectData->ClassDeviceObject =
            WdfDeviceWdmGetDeviceObject(hDevice);

        connectData->ClassService =
            MouFilter_ServiceCallback;

        break;

    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        status = STATUS_NOT_IMPLEMENTED;
        break;

    default:
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    MouFilter_DispatchPassThrough(
        Request,
        WdfDeviceGetIoTarget(hDevice)
    );
}

VOID
MouFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
)
{
    PDEVICE_EXTENSION devExt;
    WDFDEVICE hDevice;
    PMOUSE_INPUT_DATA currentInput;
    ULONG consumedTotal = 0;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);

    if (hDevice == NULL) {
        if (InputDataConsumed) {
            *InputDataConsumed = 0;
        }
        return;
    }

    devExt = FilterGetData(hDevice);

    if (devExt == NULL ||
        devExt->UpperConnectData.ClassService == NULL ||
        devExt->UpperConnectData.ClassDeviceObject == NULL) {

        if (InputDataConsumed) {
            *InputDataConsumed = 0;
        }
        return;
    }

    for (currentInput = InputDataStart;
        currentInput != InputDataEnd;
        currentInput++)
    {
        if (currentInput->ButtonFlags & (MOUSE_WHEEL | MOUSE_HWHEEL))
        {
            PWHEEL_QUEUE_ENTRY entry;

            entry = (PWHEEL_QUEUE_ENTRY)ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                sizeof(WHEEL_QUEUE_ENTRY),
                MOUFILTER_TAG
            );

            if (entry != NULL)
            {
                KIRQL oldIrql;
                BOOLEAN startTimer = FALSE;
                LARGE_INTEGER now;

                RtlZeroMemory(entry, sizeof(WHEEL_QUEUE_ENTRY));

                entry->Packet = *currentInput;
                entry->Packet.LastX = 0;
                entry->Packet.LastY = 0;

                KeAcquireSpinLock(&devExt->WheelLock, &oldIrql);

                InsertTailList(
                    &devExt->WheelQueue,
                    &entry->ListEntry
                );

                devExt->WheelQueueCount++;

                DbgPrint(
                    "[QUEUE] queue=%ld delta=%d\n",
                    devExt->WheelQueueCount,
                    (SHORT)entry->Packet.ButtonData
                );

                if (devExt->WheelTimerRunning == FALSE)
                {
                    now = KeQueryPerformanceCounter(NULL);

                    devExt->WheelTimerRunning = TRUE;
                    devExt->LastReleaseQpc.QuadPart = 0;
                    devExt->NextReleaseQpc.QuadPart =
                        now.QuadPart + MsToQpcTicks(devExt, MOUFILTER_TARGET_DELAY_MS);

                    startTimer = TRUE;
                }

                KeReleaseSpinLock(&devExt->WheelLock, oldIrql);

                if (startTimer)
                {
                    LARGE_INTEGER dueTime;

                    dueTime.QuadPart =
                        -10 * 1000 * MOUFILTER_PREWAKE_MS;

                    KeSetTimer(
                        &devExt->WheelTimer,
                        dueTime,
                        &devExt->WheelDpc
                    );
                }
            }

            consumedTotal++;
            continue;
        }

        {
            ULONG consumed = 0;

            (*(PSERVICE_CALLBACK_ROUTINE)
                devExt->UpperConnectData.ClassService)(
                    devExt->UpperConnectData.ClassDeviceObject,
                    currentInput,
                    currentInput + 1,
                    &consumed
                    );
        }

        consumedTotal++;
    }

    if (InputDataConsumed) {
        *InputDataConsumed = consumedTotal;
    }
}

VOID
MouFilter_WheelDpcFunc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
)
{
    PDEVICE_EXTENSION devExt;
    PLIST_ENTRY listEntry = NULL;
    PWHEEL_QUEUE_ENTRY wheelEntry = NULL;
    MOUSE_INPUT_DATA packet;
    ULONG consumed = 0;
    BOOLEAN hasItem = FALSE;
    BOOLEAN stopTimer = FALSE;
    LARGE_INTEGER now;
    LONGLONG maxSpinTicks;
    LONGLONG spinStart;
    LONGLONG target;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    devExt = (PDEVICE_EXTENSION)DeferredContext;

    if (devExt == NULL) {
        return;
    }

    KeAcquireSpinLockAtDpcLevel(&devExt->WheelLock);

    if (IsListEmpty(&devExt->WheelQueue))
    {
        devExt->WheelTimerRunning = FALSE;
        devExt->NextReleaseQpc.QuadPart = 0;
        stopTimer = TRUE;

        KeReleaseSpinLockFromDpcLevel(&devExt->WheelLock);

        if (stopTimer) {
            KeCancelTimer(&devExt->WheelTimer);
        }

        return;
    }

    target = devExt->NextReleaseQpc.QuadPart;

    KeReleaseSpinLockFromDpcLevel(&devExt->WheelLock);

    maxSpinTicks = UsToQpcTicks(devExt, MOUFILTER_MAX_SPIN_US);
    now = KeQueryPerformanceCounter(NULL);
    spinStart = now.QuadPart;

    while (now.QuadPart < target)
    {
        if ((now.QuadPart - spinStart) >= maxSpinTicks) {
            break;
        }

        YieldProcessor();
        now = KeQueryPerformanceCounter(NULL);
    }

    RtlZeroMemory(&packet, sizeof(MOUSE_INPUT_DATA));

    KeAcquireSpinLockAtDpcLevel(&devExt->WheelLock);

    if (!IsListEmpty(&devExt->WheelQueue))
    {
        listEntry = RemoveHeadList(&devExt->WheelQueue);

        wheelEntry = CONTAINING_RECORD(
            listEntry,
            WHEEL_QUEUE_ENTRY,
            ListEntry
        );

        packet = wheelEntry->Packet;
        devExt->WheelQueueCount--;

        hasItem = TRUE;
    }

    now = KeQueryPerformanceCounter(NULL);

    if (devExt->LastReleaseQpc.QuadPart != 0)
    {
        LONGLONG diff;
        LONGLONG ms;

        diff = now.QuadPart - devExt->LastReleaseQpc.QuadPart;
        ms = (diff * 1000) / devExt->QpcFrequency.QuadPart;

        DbgPrint("[HYBRID_INTERVAL] %lld ms queue=%ld\n", ms, devExt->WheelQueueCount);
    }

    devExt->LastReleaseQpc = now;

    if (!IsListEmpty(&devExt->WheelQueue))
    {
        LARGE_INTEGER dueTime;

        devExt->NextReleaseQpc.QuadPart =
            devExt->NextReleaseQpc.QuadPart +
            MsToQpcTicks(devExt, MOUFILTER_TARGET_DELAY_MS);

        dueTime.QuadPart =
            -10 * 1000 * MOUFILTER_PREWAKE_MS;

        KeSetTimer(
            &devExt->WheelTimer,
            dueTime,
            &devExt->WheelDpc
        );
    }
    else
    {
        devExt->WheelTimerRunning = FALSE;
        devExt->NextReleaseQpc.QuadPart = 0;
        devExt->LastReleaseQpc.QuadPart = 0;
        stopTimer = TRUE;
    }

    KeReleaseSpinLockFromDpcLevel(&devExt->WheelLock);

    if (hasItem)
    {
        ExFreePoolWithTag(wheelEntry, MOUFILTER_TAG);

        if (devExt->UpperConnectData.ClassService != NULL &&
            devExt->UpperConnectData.ClassDeviceObject != NULL)
        {
            (*(PSERVICE_CALLBACK_ROUTINE)
                devExt->UpperConnectData.ClassService)(
                    devExt->UpperConnectData.ClassDeviceObject,
                    &packet,
                    &packet + 1,
                    &consumed
                    );
        }
    }

    if (stopTimer) {
        KeCancelTimer(&devExt->WheelTimer);
    }
}

VOID
MouFilter_EvtDeviceContextCleanup(
    _In_ WDFOBJECT DeviceObject
)
{
    PDEVICE_EXTENSION devExt;
    KIRQL oldIrql;

    devExt = FilterGetData((WDFDEVICE)DeviceObject);

    KeCancelTimer(&devExt->WheelTimer);
    KeFlushQueuedDpcs();

    KeAcquireSpinLock(&devExt->WheelLock, &oldIrql);

    while (!IsListEmpty(&devExt->WheelQueue))
    {
        PLIST_ENTRY listEntry;
        PWHEEL_QUEUE_ENTRY wheelEntry;

        listEntry = RemoveHeadList(&devExt->WheelQueue);

        wheelEntry = CONTAINING_RECORD(
            listEntry,
            WHEEL_QUEUE_ENTRY,
            ListEntry
        );

        ExFreePoolWithTag(wheelEntry, MOUFILTER_TAG);
    }

    devExt->WheelQueueCount = 0;
    devExt->WheelTimerRunning = FALSE;
    devExt->NextReleaseQpc.QuadPart = 0;
    devExt->LastReleaseQpc.QuadPart = 0;

    KeReleaseSpinLock(&devExt->WheelLock, oldIrql);
}

#pragma warning(pop)