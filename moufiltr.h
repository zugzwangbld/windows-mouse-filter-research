#ifndef MOUFILTER_H
#define MOUFILTER_H

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <wdf.h>

#define MOUFILTER_TARGET_DELAY_MS 20
#define MOUFILTER_PREWAKE_MS 15
#define MOUFILTER_MAX_SPIN_US 7000
#define MOUFILTER_TAG 'lfWM'

#if DBG
#define TRAP() DbgBreakPoint()
#define DebugPrint(_x_) DbgPrint _x_
#else
#define TRAP()
#define DebugPrint(_x_)
#endif

typedef struct _WHEEL_QUEUE_ENTRY
{
    LIST_ENTRY ListEntry;
    MOUSE_INPUT_DATA Packet;
} WHEEL_QUEUE_ENTRY, * PWHEEL_QUEUE_ENTRY;

typedef struct _DEVICE_EXTENSION
{
    CONNECT_DATA UpperConnectData;

    KSPIN_LOCK WheelLock;

    LIST_ENTRY WheelQueue;
    LONG WheelQueueCount;

    KTIMER WheelTimer;
    KDPC WheelDpc;
    BOOLEAN WheelTimerRunning;

    LARGE_INTEGER QpcFrequency;
    LARGE_INTEGER NextReleaseQpc;
    LARGE_INTEGER LastReleaseQpc;

} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, FilterGetData)

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD MouFilter_EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL MouFilter_EvtIoInternalDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP MouFilter_EvtDeviceContextCleanup;

VOID MouFilter_DispatchPassThrough(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

VOID MouFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
);

VOID MouFilter_WheelDpcFunc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
);

#endif