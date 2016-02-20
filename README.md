# highpertimer
High Precision Timer Library

###Introduction

A unified High Performance Timer and a corresponding software library offer a unified interface to the known time counters and automatically identify the fastest and most reliable time source, available in the user space of a computing system. The research is focused on developing an approach of unified time acquisition from the PC hardware and accordingly substituting the common way of getting the time value through Linux system calls. The presented approach provides a much faster means of obtaining the time values with a nanosecond precision than by using conventional means. Moreover, it is capable of handling the sequential time value, precise sleep functions and process resuming. This ability means the reduction of wasting
computer resources during the execution of a sleeping process from 100% (busy-wait) to 1-1.5%, whereas the benefits of very
accurate process resuming times on long waits are maintained.

###Project structure

Please consider that we have the following modules:

* app: test applications
* lib: library files
* doc: documentation files in doxygen format
* scripts: only for versioning of the library 

### Required software
In order to build you will need: gcc 4.7.X (or later)

### License
see the LICENSE file

