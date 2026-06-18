# Moufiltr Research Project

## Overview

This repository contains my learning and research work based on Microsoft's WDK `moufiltr` sample driver.

The goal of the project was to understand how Windows processes mouse input events, how filter drivers work, and how wheel input can be intercepted and modified inside the Windows kernel.

This is an educational project and is not intended for production use.

## Technologies

* C/C++
* Windows Driver Kit (WDK)
* KMDF
* Visual Studio
* DebugView
* Windows HID Stack

## What I Learned

* Building and deploying KMDF drivers
* Configuring UpperFilters for Mouse devices
* Understanding the Windows mouse input stack
* Working with MOUSE_INPUT_DATA structures
* Intercepting wheel events in MouFilter_ServiceCallback
* Using KTIMER and KDPC
* Creating kernel threads with PsCreateSystemThread
* Working with KEVENT and synchronization
* Debugging kernel drivers with DebugView

## Device Stack

mouclass
↓
moufiltr
↓
mouhid
↓
hidusb

## Project Status

Research / Learning Project

The project was created for educational purposes to explore Windows kernel development and input processing.
