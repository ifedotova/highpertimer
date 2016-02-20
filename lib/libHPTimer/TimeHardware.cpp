/*
 * @file   TimeHardware.cpp 
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


#include <iostream>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "TimeHardware.h"

// C++ macro for one billion ( 10^9 )
constexpr uint64_t ONE_BILLION = 1000000000LL;

// input eax values specifying which part of information to return within CPUID call
constexpr uint64_t EaxForCPUIDSign  = 0x80000000;
constexpr uint64_t EaxForInvarTSC   = 0x80000007;
constexpr uint64_t EaxForRDTSCP     = 0x80000001;
constexpr uint64_t EaxForBrand      = 0x80000002;
constexpr uint64_t EaxForVendor     = 0;

// offset value from the first mapped memory address for main counter register
constexpr uint64_t MainCounterOffset = 0x0f0;
// offset value from the first mapped memory address for main counter register
constexpr uint64_t MainCounterOffsetHigh = 0x0f4;
// the value is added to hpet_Mcounter.
constexpr uint64_t PeriodOffset = 0x004;
// constant string for Cenatur vendor "CentaurHauls"
constexpr char CentaurVendor[12] = {'C', 'e', 'n', 't','a','u', 'r', 'H', 'a', 'u', 'l', 's'};
// constant string for Intel vendor "GenuineIntel"
constexpr char IntelVendor[12] = {'G', 'e', 'n', 'u', 'i', 'n', 'e', 'I', 'n', 't', 'e', 'l'};

namespace HPTimer
{

// instantiation of all the static variables
char TSCTimer::BrandString[48];
char TSCTimer::VendorString[12];
bool TSCTimer::HasRDTSCPinst;
bool TSCTimer::HasInvariantTSC;
bool TSCTimer::HasConstantTSC;

HPETFail HPETTimer::HPETFailReason;
int HPETTimer::HpetFd;
unsigned char* HPETTimer::HpetAdd_ptr;
uint64_t HPETTimer::HpetPeriod;

uint32_t OSTimer::OSTimerFrequency ( 1000 );

class HighPerTimer;

//////////////////////////////////////////////////////////////////
// TSCTimer
//////////////////////////////////////////////////////////////////

/// initialize the TSC subsystem of our timer
bool TSCTimer::InitTSCTimer()
{
    #ifdef  __arm__
    // This is ARM assembly code which is preferred architecture
    // ARM doesn't support neither TSC nor HPET
        return false;
    #endif
    // the format of the 32-bit processor signature output
    struct SignCPUID
    {
    uint32_t   Stepping         :
        4;
    uint32_t   Model            :
        4;
    uint32_t   FamilyID         :
        4;
    uint32_t   Type             :
        2;
    uint32_t   Reserved1        :
        2;
    uint32_t   ExtendedModel    :
        4;
    uint32_t   ExtendedFamilyID :
        8;
    uint32_t   Reserved2        :
        4;
    };
    SignCPUID SignCPUIDoutput;

    // identify the processor using the CPUID instruction
    // when called with EAX = 0x80000000, CPUID returns the highest calling parameter that the CPU supports.
    RegsCPUID RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( EaxForCPUIDSign );
    if ( ( RegsCPUIDoutput.EAXBuf < EaxForInvarTSC ) )
    { // invariant TSC can't be tested, so it can't be true
        TSCTimer::HasInvariantTSC = false;
    }

    TSCTimer::GetVendorID();
    TSCTimer::GetCPUBrand();

    // check the presence of the RDTSCP indicated by CPUID leaf 80000001, EDX bit 27
    RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( EaxForRDTSCP );
    if ( ( RegsCPUIDoutput.EDXBuf >> 27 ) & 1 )
    {
        TSCTimer::HasRDTSCPinst = true;
    }
    else
    {
        TSCTimer::HasRDTSCPinst = false;
    }

    // get CPUID output with EAX = 1 to fill in CPU signature struct with values of Model, Family, Stepping and Type of processor.
    RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( 1 );

    // assign CPUID output instruction to the struct of CPUID signature (convert int to pointer)
    * ( reinterpret_cast<uint32_t*> ( &SignCPUIDoutput ) ) = RegsCPUIDoutput.EAXBuf;

    RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( EaxForInvarTSC );
    if ( ( ( RegsCPUIDoutput.EDXBuf >> 8 ) & 1 ) )
    {
        TSCTimer::HasInvariantTSC = true;
        TSCTimer::HasConstantTSC = true;
        return true;
    }
    // check models with constant tsc
    // for Intel vendor
    else if ( !memcmp ( VendorString, IntelVendor, 12 ) )
    {
        // since family 0x0f and model 0x03 all have constant tsc
        // including Pentium 4, Intel Xeon processors
        if ( ( ( SignCPUIDoutput.FamilyID + SignCPUIDoutput.ExtendedFamilyID ) == 0x0f ) && ( ( SignCPUIDoutput.ExtendedModel << 4|SignCPUIDoutput.Model ) >= 0x03 ) )
        {
            TSCTimer::HasInvariantTSC = false;
            TSCTimer::HasConstantTSC = true;
            return true;
        }
        // either since family 0x06 and model 0x0e all have constant tsc
        // including Intel Core Solo, Intel Core Duo, Intel Xeon 5100, Intel Core 2 Duo. Intel Core 2, Intel Atom
        if ( ( ( SignCPUIDoutput.FamilyID + SignCPUIDoutput.ExtendedFamilyID ) == 0x06 ) && ( ( SignCPUIDoutput.ExtendedModel << 4|SignCPUIDoutput.Model ) >= 0x0e ) )
        {
            TSCTimer::HasInvariantTSC = false;
            TSCTimer::HasConstantTSC = true;
            return true;
        }
    }
    // for Centaur vendor including VIA processors
    else if ( !memcmp ( VendorString, CentaurVendor, 12 ) )
    {
        // since family 0x06 and model 0x0f either all have constant tsc
        if ( ( ( SignCPUIDoutput.FamilyID + SignCPUIDoutput.ExtendedFamilyID ) == 0x06 ) && ( ( SignCPUIDoutput.ExtendedModel << 4|SignCPUIDoutput.Model ) >= 0x0f ) )
        {
            TSCTimer::HasInvariantTSC = false;
            TSCTimer::HasConstantTSC = true;
            return true;
        }
    }
    TSCTimer::HasInvariantTSC = false;
    TSCTimer::HasConstantTSC = false;
    return false;
}

// perform CPU ID instruction
// param InputEAX is the input value of EAX register specifying which part of  information to return
TSCTimer::RegsCPUID TSCTimer::ExecuteCPUID ( uint32_t InputEAX )
{
#ifndef __arm__
    RegsCPUID RegsCPUIDoutput;
    asm volatile
    (
        " cpuid;"
    : "=a" ( RegsCPUIDoutput.EAXBuf ), "=b" ( RegsCPUIDoutput.EBXBuf ), "=c" ( RegsCPUIDoutput.ECXBuf ), "=d" ( RegsCPUIDoutput.EDXBuf )
    : "a" ( InputEAX )
    );
    return RegsCPUIDoutput;
#endif
}

// get processor brand string
void TSCTimer::GetCPUBrand()
{
    memset ( TSCTimer::BrandString, 0, 48 );
    for ( uint32_t i ( 0 ); i < 3; i++ )
    {
        // when EAX = 0x80000002, 0x80000003, 0x80000004, CPUID returns processor brand string
        RegsCPUID RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( EaxForBrand + i );
        // the bytes from eax to edx will be saved as Brandstring
        memcpy ( TSCTimer::BrandString + i*16, &RegsCPUIDoutput.EAXBuf, 16 );
    }
}

// get the CPU's manufacturers ID string
void TSCTimer::GetVendorID()
{
    memset ( TSCTimer::VendorString, 0,  12 );
    // when called with EAX = 0, CPUID returns the vendor ID string in EBX, EDX and ECX.
    RegsCPUID RegsCPUIDoutput = TSCTimer::ExecuteCPUID ( EaxForVendor );
    // for Intel Vendor it returns value in EBX ("Genu"), in EDX ("ineI"), in ECX ("ntel") 
    memcpy ( TSCTimer::VendorString,   &RegsCPUIDoutput.EBXBuf,4 );
    memcpy ( TSCTimer::VendorString+4, &RegsCPUIDoutput.EDXBuf,4 );
    memcpy ( TSCTimer::VendorString+8, &RegsCPUIDoutput.ECXBuf,4 );
}

//////////////////////////////////////////////////////////////////
// HPETTimer
//////////////////////////////////////////////////////////////////

// check availability of hpet timer
bool HPETTimer::InitHPETTimer()
{
    // open the device of HPET
    HPETTimer::HpetFd = ::open ( "/dev/hpet", O_RDONLY );
    if ( HPETTimer::HpetFd < 0 )
    {
        switch ( errno )
        {
        case EACCES:
            HPETTimer::HPETFailReason = HPETFail::ACCESS;
            break;
        case EFAULT:
            HPETTimer::HPETFailReason = HPETFail::FAULT;
            break;
        case ENOENT:
            HPETTimer::HPETFailReason = HPETFail::NOENT;
            break;
        case EMFILE:
            HPETTimer::HPETFailReason = HPETFail::MFILE;            
            break;
        case EBUSY:
            HPETTimer::HPETFailReason = HPETFail::BUSY;
            break;
        default:
            HPETTimer::HPETFailReason = HPETFail::UNKNOWN;;
            break;
        }
        return false;
    }
    // mmap the hpet-circuit into virtual memory, if it succeeds, the first address of virtual memory will be returned in HpetAdd_ptr pointer
    HPETTimer::HpetAdd_ptr = ( unsigned char * ) mmap ( NULL, 1024, PROT_READ, MAP_SHARED, HPETTimer::HpetFd, 0 );
    if ( HPETTimer::HpetAdd_ptr == MAP_FAILED )
    {   
        switch ( errno )
        {
        case EACCES:
            HPETTimer::HPETFailReason = HPETFail::ACCESS;
            break;
        case EAGAIN:
            HPETTimer::HPETFailReason = HPETFail::AGAIN;
            break;
        case EBADF:
            HPETTimer::HPETFailReason = HPETFail::BADF;
            break;
        case ENODEV:
            HPETTimer::HPETFailReason = HPETFail::NODEV;
            break;
        case ENOMEM:
            HPETTimer::HPETFailReason = HPETFail::NOMEM;
            break;
        default:
            HPETTimer::HPETFailReason = HPETFail::UNKNOWN;;
            break;
        }
        int retval = ::close ( HPETTimer::HpetFd );
        return false;
    }
    // 32-bit HPET main counter overruns every 7,16 minutes. So it is denied using this source.
    uint64_t HpetMcounterSize =  * ( ( int64_t * ) ( HPETTimer::HpetAdd_ptr ) ) >> 13;
    // if this bit is a 0, it indicates that the main counter is 32 bits wide
    if ( ! HpetMcounterSize )
    {
        HPETTimer::HPETFailReason = HPETFail::MC32BIT;
        return false;
    }
    return true;
}

// get hpet timer tics
int64_t HPETTimer::GetHPETTics()
{
#ifdef __x86_64__
    uint64_t HpetMcounter =  * ( reinterpret_cast<int32_t*> ( HPETTimer::HpetAdd_ptr + MainCounterOffset ) );
    return HpetMcounter;
#else
    // the case when we have the 64 bit main counter, but a 32 bit addresses, meaning reading the main counter must be performed within two memory accesses
    while ( 1 )
    {
        // Multiple reading HPET main counter register
        // avoid an accuracy problem which may be arise if just after reading one half, the other half rolls over and changes the first half.

        uint64_t Hpet_High_Order = * ( reinterpret_cast<int32_t*> ( HPETTimer::HpetAdd_ptr + MainCounterOffsetHigh ) );
        uint64_t Hpet_Low_Order  = * ( reinterpret_cast<int32_t*> ( HPETTimer::HpetAdd_ptr + MainCounterOffset ) );
        uint64_t _Hpet_High_Order = * ( reinterpret_cast<int32_t*> ( HPETTimer::HpetAdd_ptr + MainCounterOffsetHigh ) );
        // if both read from the high 32 bits of the main counter register HPET equal, clock cycles are current returned
        if ( Hpet_High_Order == _Hpet_High_Order )
        {
            return ( Hpet_High_Order << 32 | Hpet_Low_Order );
        }
    }
#endif
}

// set the the value of hpet frequency
double HPETTimer::GetHPETFrequency()
{
    // read the high 32-bit-order of the general capabilities and ID Register, period offset 0x004
    HPETTimer::HpetPeriod = * ( ( uint32_t * ) ( HPETTimer::HpetAdd_ptr + PeriodOffset ) );
    // get the HPET frequency in microseconds
    return ( double ) ONE_BILLION / HPETTimer::HpetPeriod;
}

//////////////////////////////////////////////////////////////////
// OS Timer
//////////////////////////////////////////////////////////////////

// get OS timer tics
int64_t OSTimer::GetOSTimerTics()
{
    timespec ts;
    clock_gettime ( CLOCK_MONOTONIC, &ts );
    return ( static_cast<int64_t> ( ts.tv_sec ) * ONE_BILLION + static_cast<int64_t> ( ts.tv_nsec ) ) * ( OSTimer::OSTimerFrequency / 1000LL );
}

} // end namespace HPTimer


