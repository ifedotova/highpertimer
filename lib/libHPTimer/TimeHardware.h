/*
 * @file   TimeHardware.h 
 * @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
 * @date   Apr, 2012
 * @brief  Initialization routine of timing hardware and acquisition of time information
 *  
 * Copyright (C) 2012-2016,  Future Internet Lab Anhalt (FILA),
 * Anhalt University of Applied Sciences, Koethen, Germany. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the Artistic License 2.0 as published by the Free Software 
 * Foundation with classpath exception.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the Artistic License version 2.0 for more
 * details.
 *
 * You should have received a copy of the Artistic License along with this
 * program (please see the COPYING file); 
 */


#ifndef _TIMEHARDWARE_H
#define _TIMEHARDWARE_H
#include <stdint.h>

namespace HPTimer
{

//! Class for CPU identification and checking features of Time Stamp Counter.
/*!
 * The Time Stamp Counter is a 64-bit register present on all x86 processors since the Pentium.
 * It counts the number of cycles since a reset and its rate depends on the CPU frequency
 */
class TSCTimer
{
    friend class HighPerTimer;
    friend class AccessTimeHardware;
public:
    /// exclude creating any instance of TSCTimer class. Shall be used only via the friend HighPerTimer class
    TSCTimer() = delete;        

private:
    /// call CPUID instruction and obtain appropriate information such as processor vendor and brand,
    /// check wether a processor has rdtscp instruction, invariant tsc flag and weather tsc has a constant rate,
    /// return true if TSC counter is available, stable and returns correct time value
    static bool InitTSCTimer();

    /// storage for saving the value of 32 bits registers to obtain basic CPUID information:
    /// EAX (Accumulator Register), EBX (Base Register), ECX (Counter Register), EDX (Data Register).
    struct RegsCPUID
    {
        uint32_t EAXBuf;
        uint32_t EBXBuf;
        uint32_t ECXBuf;
        uint32_t EDXBuf;
    };
    /** perform CPUID instruction
     * @param InputEAX [in] ]value of EAX register specifying what information to return
     * @return filled result, which CPUID instruction has written into its registers
     */
    static RegsCPUID ExecuteCPUID ( uint32_t InputEAX );
    /// vendor information string in the array
    static char VendorString[12];
    /// CPU brand string
    static char BrandString[48];

    /// get processor brand string. The result is written as a side effect to BrandSTring
    static void GetCPUBrand();
    /// get the CPU's manufacturers ID string. The result is written as a side effect to VendorString
    static void GetVendorID();

    /// presence of RDTSCP assembly instruction:
    /// The RDTSCP instruction waits until all previous instructions have been executed before reading the TSC counter.
    /// However, subsequent instructions may begin execution before the read operation is performed.
    static bool HasRDTSCPinst;

    /// presence of Invariant TSC:
    /// Invariant TSC behavior ensures that the counter has constant rate
    /// do not bind tight to processor cores and their cycles.
    static bool HasInvariantTSC;

    /// presence of Constant TSC:
    /// Constant TSC behavior ensures that the duration of each clock tick is uniform
    /// and supports the use of the TSC as a wall clock timer even if the processor core changes frequency.
    static bool HasConstantTSC;

    /// read the tsc counter introduced as the instruction RDTSC or RDTSCP
    /// it depends on whether your code is 64bit or 32bit
    inline static int64_t CPU_TSC()
    {
#ifndef __arm__
        // The RDTSC(P) instruction loads the high-order 32 bits of the timestamp register into EDX, and the low-order 32 bits into EAX. 
        register uint32_t low32, high32;
        if ( TSCTimer::HasRDTSCPinst )
        {
            asm volatile
            (
                "RDTSCP\n\t"       
            : "=a" ( low32 ), "=d" ( high32 )
            );         
        }
        else
        {
            asm volatile
            (
                "RDTSC\n\t"
            : "=a" ( low32 ), "=d" ( high32 )
            );            
        }
        return (uint64_t) high32 << 32  | low32;
#endif
        return 0;
    };
};

//! reasons for hpet unavailability: 
/*! 
 * EACCESS - permission denied. The requested access to the file is not allowed, or search permission is denied for one of the directories in the path prefix of pathname +
 * EFAULT  - pathname points outside your accessible address space
 * ENOENT  - file or path not found. The named file does not exist. Or, a directory component in pathname does not exist or is a dangling symbolic link
 * EMFILE  - no file handle available. The process already has the maximum number of files open
 * EBUSY   - device or resource busy +
 * 
 * EAGAIN - the file has been locked, or too much memory has been locked
 * EBADF  - fd is not a valid file descriptor +
 * ENODEV - the underlying file system of the specified file does not support memory mapping
 * ENOMEM - no memory is available, or the process's maximum number of mappings would have been exceeded
 * 
 * MC32BIT - inability for 32-bit main counter operates in 64-bit system mode
 * UNKNOWN - unknown error
 */
enum class HPETFail
{
    ACCESS, FAULT, NOENT, MFILE, AGAIN, BUSY, BADF, NODEV, NOMEM, MC32BIT, UNKNOWN
};

//! Class for the High Precision Event Timer initialization
 /*! Initialize HPET and get access to the HPET main counter
 * HPET have a constant frequency of at least 10 MHz and a set of comparators, which can generate an interrupt.
 */
class HPETTimer
{
    friend class HighPerTimer;
    friend class HPTimerInitAndClean;
    friend class AccessTimeHardware;
private:
    /// exclude creating any instance of HPETTimer class  Shall be used only via the friend HighPerTimer class
    HPETTimer() = delete;
    /// possible reasons for fail initialization of hpet device
    static HPETFail HPETFailReason;
    /// check availability of hpet device,
    /// accessible only to class HighPerTimer (functions HighPerTimer::InitHPTimerSource and HighPerTimer::SetTimerSource)
    static bool InitHPETTimer();
    /// get hpet timer tics
    static int64_t GetHPETTics();
    /// get the value of hpet frequency
    static double GetHPETFrequency();
    /// file handle to open hpet device
    static int HpetFd;
    /// for getting hpet frequency
    static uint64_t HpetPeriod;
    /// pointer to the first mapped memory address of the HPET table
    static unsigned char *HpetAdd_ptr;
};


//! Class for initialization of system call of clock_gettime() to get OS time
 /*! Provide access to a system-wide realtime clock with the resolution of nanoseconds
 */
class OSTimer
{
    friend class HighPerTimer;
public:
    /// exclude creating any instance of OSTimer class
    OSTimer() = delete;
private:
    /// the value of OS timer frequency
    static uint32_t OSTimerFrequency;
    /// get OS timer tics
    static int64_t GetOSTimerTics();

};

} // end namespace HPTimer

#endif //_TIMEHARDWARE_H



