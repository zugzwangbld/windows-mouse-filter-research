# Moufiltr Research Project

## Overview

This repository contains my learning and research work based on Microsoft's WDK Moufiltr sample driver.

The goal of the project was to understand how Windows processes mouse input, how mouse filter drivers work, and how wheel events can be intercepted and modified inside the Windows kernel.

This project is intended for educational and research purposes only and is not production-ready.

## Technologies

* C/C++
* Windows Driver Kit (WDK)
* KMDF
* Visual Studio
* DebugView
* Windows HID Stack

## What I Learned

* Building and deploying KMDF drivers
* Installing and removing mouse filter drivers
* Working with UpperFilters
* Understanding the Windows mouse input stack
* Understanding the relationship between:

  * mouclass
  * moufiltr
  * mouhid
  * hidusb
* Intercepting mouse wheel events
* Working with MOUSE_INPUT_DATA structures
* Debugging kernel drivers with DebugView

## Experiments

### Wheel Event Interception

Wheel events were intercepted inside:

MouFilter_ServiceCallback

using:

* MOUSE_INPUT_DATA
* ButtonFlags
* ButtonData
* MOUSE_WHEEL

### Timer-Based Processing

Tested delayed wheel event playback using:

* KTIMER
* KDPC
* KeSetTimerEx

### Thread-Based Processing

Tested an alternative implementation using:

* PsCreateSystemThread
* KEVENT
* KeWaitForSingleObject
* KeDelayExecutionThread

### Event Queue Research

Several approaches were tested:

* WheelDeltaAccum
* WheelQueueCount
* Timer-based scheduling
* Thread-based scheduling

A future improvement would be implementing a proper FIFO ring buffer for wheel events.

## Lessons Learned

During the project I discovered several practical limitations:

* Windows timer resolution affects scheduling accuracy.
* Driver-generated timing does not always match timing observed by applications.
* A simple event counter is not a true FIFO queue.
* Filter drivers are useful for interception and modification but are not ideal for precise event replay.

## Future Work

Potential next steps:

* Implement a real ring buffer queue.
* Improve thread shutdown and cleanup.
* Reduce debug logging.
* Explore Virtual HID Device development.

## Project Status

Learning / Research Project

The project was created to learn Windows kernel development, input processing, synchronization, timers, threads and mouse filter drivers.
