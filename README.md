# Windows Mouse Filter Research

## Overview

This repository contains my learning and research work based on Microsoft's WDK Moufiltr sample driver.

The goal of the project was to understand how Windows processes mouse wheel input, how filter drivers work, and how wheel events can be intercepted, delayed and replayed inside the Windows kernel.

This project is intended for educational and research purposes only and is not production-ready.

## What Was Explored

* Windows Driver Kit (WDK)
* KMDF mouse filter drivers
* UpperFilters
* Mouse input stack:

  * mouclass
  * moufiltr
  * mouhid
  * hidusb
* MOUSE_INPUT_DATA
* MOUSE_WHEEL
* Kernel timers (KTIMER, KDPC)
* Kernel threads (PsCreateSystemThread)
* Synchronization (KSPIN_LOCK, KEVENT)

## Experiments

### Wheel Event Interception

Wheel events were intercepted inside:

MouFilter_ServiceCallback

using:

* MOUSE_INPUT_DATA
* ButtonFlags
* ButtonData
* MOUSE_WHEEL

### Delayed Playback

Several approaches were tested:

* WheelDeltaAccum
* KTIMER + KDPC
* Kernel thread + KEVENT
* Event queue experiments

### Lessons Learned

The project demonstrated that:

* Windows timer resolution affects scheduling accuracy.
* A simple event counter is not a true FIFO queue.
* Filter drivers are useful for interception and modification.
* Virtual HID devices may be a better architecture for precise event replay.

## Project Status

Learning / Research Project

This repository documents experiments with Windows mouse filter drivers and Windows kernel development.
