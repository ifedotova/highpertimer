/*
 * @file   HighPerTimer.cpp 
 * @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
 * @date   Apr, 2012
 * @brief  Main routine of handling time value along with the access to timing hardware attributes
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


#include <cstring> 
#include <sstream>
#include <string>
#include <iomanip>    
#include <unistd.h>   
#include <iostream>
#include <vector>
#include <numeric>    
#include <algorithm> 
#include <fstream>    
#include <sys/resource.h> 
#include <sys/time.h>    
#include <thread>
#include <chrono>
#include <condition_variable>
#include "HighPerTimer.h"

// type saver for big numbers
constexpr double ONE_QUADRILLION = 1000000000000000.0; // C++ macro for one quadrillion ( 10^15 )
constexpr uint64_t ONE_BILLION = 1000000000LL; // C++ macro for one billion ( 10^9 )
constexpr uint64_t ONE_MILLION = 1000000LL; // C++ macro for one million ( 10^6 )

// the clock interrupt frequency of the particular hardware platform
constexpr uint32_t HZ1000 = 1000;
constexpr uint32_t HZ300 = 300;
constexpr uint32_t HZ250 = 250;
constexpr uint32_t HZ100 = 100;
 

namespace HPTimer
{
// NOTE: keep this declaration BEFORE instance of the HPTimerInitAndClean class, because there it is setted ticks to max and min objects
HighPerTimer HighPerTimer::HPTimer_MAX;
HighPerTimer HighPerTimer::HPTimer_MIN;

// start to initialize static HighPerTimer class members in strict order within appropriate ctor
static HPTimerInitAndClean the_HPTimerInitAndClean;

TimeSource HighPerTimer::HPTimerSource;
int64_t HighPerTimer::TicsPerUsec;
double HighPerTimer::NsecPerTic;
int64_t HighPerTimer::UnixZeroShift;
double HighPerTimer::HPJiffies;

// variables for implementing the mechanism of sleep and interruption 
std::condition_variable HPcond;
std::mutex HPmutex;

// counter of the number of failures during initializing frequency InitHPFrequency(). 
// if the first attempt to initialize frequency was failed, we do reinitialize and recursion.
// recursion is allowed to do only three times, after that abort with error.
uint32_t InitFreqAttempt(0);

// busy waiting. 
static inline void RepNop ( void )
{
#ifdef __arm__ 
// This is ARM assembly code which is preferred architecture
    asm volatile
    ( 
        "nop"
    );
#else
// This is X86 assembly code    
    asm volatile
    ( 
        "rep;nop"     
    );
#endif
}

// standard ctor
// set tics of HPTimer equal to zero in lazy behavior
HighPerTimer::HighPerTimer() :
        mHPTics ( 0 ),
        mNormalized ( false )
{
}

// ctor
// @param Seconds means the seconds part
// @param NSeconds means the nanoseconds part
// @param Sign means the sign of the timer (true means negative value)
// @exception std::out_of_range if a memory allocation failed
// when one of the ints is negative, than sign must be false and when NSeconds is negative, it is allowed to use only zero Seconds. Otherwise it is an illegal initialitazion values.
// Seconds and NSeconds values should be within the appropriate limits of min or max HPTimer values. Otherwise it is an HPTimer overflow.
// HighPerTimer is extracted from the Seconds and and NSeconds parts
HighPerTimer::HighPerTimer ( int64_t Seconds, int64_t NSeconds, bool Sign ) :
        mSign ( Sign ),
        mNormalized ( true )
{
    // when one of the ints is negative, than sign must be false. Otherwise we have an illegal initialitazion values.
    if ( ( Seconds < 0 || NSeconds < 0 ) && true == Sign )
    {
        throw ( std::out_of_range ( "illegal init Parameters of HighPerTimer" ) );
    }
    // when NSeconds is negative, it is allowed to use only zero Seconds. Otherwise we have an illegal initialitazion values.
    if ( NSeconds < 0 && Seconds != 0 )
    {
        throw ( std::out_of_range ( "illegal init Parameters of HighPerTimer" ) );
    }

    // bring ints to positive values
    if ( Seconds < 0 )
    {
        Seconds *= -1;
        mSign = true;
    }
    if ( NSeconds < 0 )
    {
        NSeconds *= -1;
        mSign = true;
    }

    mSeconds = Seconds + ( NSeconds / ONE_BILLION );
    // check for overflow due to too big NSeconds add
    if ( 0 > mSeconds )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNSeconds = NSeconds % ONE_BILLION;

    // check for possible overflow according to max and min value HPTimer
    if ( ( mSeconds * ONE_BILLION + mNSeconds ) > ( HighPerTimer::HPTimer_MAX.Seconds() * ONE_BILLION + HighPerTimer::HPTimer_MAX.NSeconds() ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }

    if ( mSign )
    {
        // increase a little bit NsecPerTic to avoid overflow in case we calcukate max value with TSC source
        mHPTics = - ( static_cast<int64_t> ( ( mSeconds * ONE_BILLION + mNSeconds ) / ( HighPerTimer::NsecPerTic + ( 1/ONE_QUADRILLION ) ) ) );
    }
    else
    {
        // increase a little bit NsecPerTic to avoid overflow in case we calcukate max value with TSC source
        mHPTics = static_cast<int64_t> ( ( mSeconds * ONE_BILLION + mNSeconds ) / ( HighPerTimer::NsecPerTic + ( 1/ONE_QUADRILLION ) ) );
    }
}

// ctor
// @param pHighPerTimer is the HighPerTimer counter that the timer should be set
// @param Shift tells, if the value should be shifted against the UnixZeroShift. If it is true, Unix zero shift time added to the mHPTics value
// @exception std::out_of_range if a memory allocation failed
// HPTics value should be within the appropriate limits of min or max HPTimer values. Otherwise it is an HPTimer overflow.
HighPerTimer::HighPerTimer ( int64_t HPTics, bool Shift ) :
        mHPTics ( HPTics ),
        mNormalized ( false )
{
    if ( Shift )
    {
        if ( ( HighPerTimer::HPTimer_MAX.HPTics() - HighPerTimer::UnixZeroShift  >= mHPTics ) && ( HighPerTimer::HPTimer_MIN.HPTics() + HighPerTimer::UnixZeroShift  <= mHPTics ) )
        {
            mHPTics += HighPerTimer::UnixZeroShift;
        }
        else
        {
            throw  std::out_of_range ( "HPTimer overflow" );
            return;
        }
    }
    // check for possible overflow according to max and min value HPTimer
    if ( ( mHPTics > HighPerTimer::HPTimer_MAX.HPTics() ) || ( mHPTics < HighPerTimer::HPTimer_MIN.HPTics() ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    return;
}

// ctor
// @param tv is the timevalue struct
// use a delegation to the next ctor
HighPerTimer::HighPerTimer ( const timeval & TV ) :
       HighPerTimer ( timespec { TV.tv_sec, TV.tv_usec * 1000 } )
 
{   
}

// ctor
// @param ts is the timespec struct
HighPerTimer::HighPerTimer ( const timespec & TS ) :
        mSeconds ( TS.tv_sec ),
        mNSeconds ( TS.tv_nsec ),
        mSign ( false ),
        mNormalized ( true )
{
    // check for possible overflow according to max and min value HPTimer
    if ( ( mSeconds * ONE_BILLION + mNSeconds ) > ( HighPerTimer::HPTimer_MAX.Seconds() * ONE_BILLION + HighPerTimer::HPTimer_MAX.NSeconds() ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mHPTics = static_cast<int64_t> ( ( mSeconds * ONE_BILLION + mNSeconds ) / ( HighPerTimer::NsecPerTic + ( 1/ONE_QUADRILLION ) ) );       
}

// copy ctor, use lazy assignment approach
HighPerTimer::HighPerTimer ( const HighPerTimer & Timer ) :
        mHPTics ( Timer.mHPTics ),
        mNormalized ( false )
{
  
}

// move ctor
HighPerTimer::HighPerTimer ( HighPerTimer && Timer ) :
        mHPTics ( Timer.mHPTics ),     
        mNormalized ( false )
{

}

// initialize Min and Max value of HPTimer
// when HPET is timer source, the max possible value of NSecPerTic is 100 and it can be caused a possible overflow and a loss of accuracy calculating Seconds and NSeconds part of timer
// so in HPET case it is more reliable to decrease limits for max and min HPTimer values
// when TSC or OS clock are timer sources, the NSecPerTic value is always less than zero. So it is still safe to limit max and min HPTimer values within max and min values of int64 type
void HighPerTimer::InitMaxMinHPTimer()
{
    if ( TimeSource::HPET == HighPerTimer::HPTimerSource ) // HPET case
    {
        HighPerTimer::HPTimer_MAX.SetTics ( std::numeric_limits<int64_t>::max() / 120 );
        HighPerTimer::HPTimer_MIN.SetTics ( std::numeric_limits<int64_t>::min() / 120 );
    }
    else
    {
        HighPerTimer::HPTimer_MAX.SetTics ( std::numeric_limits<int64_t>::max() );
        HighPerTimer::HPTimer_MIN.SetTics ( std::numeric_limits<int64_t>::min() );
    }
}
// initialize all kind of timer source and choose the most optimal.
// @return current time source which obtains accurate tics value
void HighPerTimer::InitTimerSource()
{
    uint32_t LoopCount ( 1000 );
    int64_t HPtimerNow1 ( 0 ), HPtimerNow2 ( 0 );
    double HPdelta ( 0 ), Percentage ( 0 );
    // value of percents means a limit which is compared with Mean percentage
    // to determine if mean values are "similar" or "different" and  deviation values should be also checked
    double Limit (25.0);

    double MeanHpet ( 0 ), MeanOs ( 0 ), SqSum ( 0 ), StDevHpet ( 0 ), StDevOs ( 0 );  
    std::vector <double> VecHpet, VecOs;

    // TSC is preferred timer, so we are checking it first
    if ( TSCTimer::InitTSCTimer() )
    {
        HighPerTimer::HPTimerSource =  TimeSource::TSC;
        return;
    }
    else if ( HPETTimer::InitHPETTimer() )
    {

        // in case tsc unavailable, check which TimerSource setting costs more: hpet or clock_gettime. It is expected to be clock_gettime there
        for ( uint32_t i ( 0 ); i < LoopCount; i++ )
        {
            HPtimerNow1 = HPETTimer::GetHPETTics();
            HPtimerNow2 = HPETTimer::GetHPETTics();
            HPdelta = HPtimerNow2 - HPtimerNow1;
            VecHpet.push_back(( HPdelta / HPETTimer::GetHPETFrequency() ) );          

            HPtimerNow1 = OSTimer::GetOSTimerTics();
            HPtimerNow2 = OSTimer::GetOSTimerTics();
            HPdelta = HPtimerNow2 - HPtimerNow1;
            VecOs.push_back(( HPdelta / OSTimer::OSTimerFrequency ) );
        }

        // sum up a range of elements, calculate mean value, express results in microseconds to compare
        MeanOs = std::accumulate(VecOs.begin(), VecOs.end(), 0.0) / LoopCount;     
        MeanHpet = std::accumulate(VecHpet.begin(), VecHpet.end(), 0.0) / LoopCount;
      
        // calculate percentage of mean values to compare
        if ( MeanHpet < MeanOs )
        {
            Percentage = 100 - ( MeanHpet / MeanOs * 100 );
        }
        else
        {
            Percentage = 100 - ( MeanOs / MeanHpet * 100 );
        }      
      
        // when the mean values are similar (the difference is no more than 25%), use standard deviation as a secondary parameter
        if ( Percentage < Limit )
        {
            SqSum = std::inner_product ( VecOs.begin(), VecOs.end(), VecOs.begin(), 0.0 );
            StDevOs = std::sqrt ( SqSum / VecOs.size() - MeanOs * MeanOs );
            SqSum = std::inner_product ( VecHpet.begin(), VecHpet.end(), VecHpet.begin(), 0.0 );
            StDevHpet = std::sqrt ( SqSum / VecHpet.size() - MeanHpet * MeanHpet );
           
            if ( StDevHpet < StDevOs)
            {
                HighPerTimer::HPTimerSource =  TimeSource::HPET;
                return;
            }
            else
            {
                HighPerTimer::HPTimerSource =  TimeSource::OS;
                return;
            }
        }
        else if ( MeanHpet < MeanOs )
        {
            HighPerTimer::HPTimerSource =  TimeSource::HPET;
            return;
        }
    }
    HighPerTimer::HPTimerSource =  TimeSource::OS;
    return;
}

// set the frequency and reciprocal value NsecPerTic depends on timer source
// @return: value of frequency time source - number of tics within one microsecond
void HighPerTimer::InitHPFrequency( const double DelayTime )
{
    // TSC case
    if ( TimeSource::TSC == HighPerTimer::HPTimerSource ) 
    {
        struct timesStruct
        {
            int64_t TSC1,TSC2 ;
	    double usFreq;            
	    timespec ts1, ts2;	   
        };
	int64_t t1(0), t2(0);
        long timeStructSize ( 5 );
	
        // perform frequency estimation 5 times,
        timesStruct times[timeStructSize];
        for ( uint32_t i ( 0 ); i < timeStructSize; i++ )
        {     
            clock_gettime ( CLOCK_REALTIME, &times[i].ts1 );
            times[i].TSC1 = HighPerTimer::CPU_Tics ();
            usleep ( DelayTime * ONE_MILLION );
            clock_gettime ( CLOCK_REALTIME, &times[i].ts2 ); 
	    times[i].TSC2 = HighPerTimer::CPU_Tics ();      
	    t1 = static_cast<int64_t> ( times[i].ts1.tv_sec ) * ONE_BILLION + static_cast<int64_t> ( times[i].ts1.tv_nsec );
	    t2 = static_cast<int64_t> ( times[i].ts2.tv_sec ) * ONE_BILLION + static_cast<int64_t> ( times[i].ts2.tv_nsec );    
	                       
	    if ( t1 != t2 )
            {
	        // nsec per tics
                times[i].usFreq = static_cast<double> ( t2 - t1 ) / ( times[i].TSC2 - times[i].TSC1 ) ;
            }
            else
            {
                times[i].usFreq = 0;
            }               
        }            
        
        // write the field of struct with frequency to vector
        std::vector <double> VecFreq;
        for ( uint32_t i ( 0 ); i < timeStructSize; i++ )
        {
            VecFreq.push_back(times[i].usFreq);
        }

        // calculate statistcs
        double mean = std::accumulate(std::begin(VecFreq), std::end(VecFreq), 0.0) / VecFreq.size();
        double accum = 0.0;
        std::for_each (std::begin(VecFreq), std::end(VecFreq), [&](const double d) {
            accum += (d - mean) * (d - mean);
        });
        double stdev = sqrt(accum / (VecFreq.size()-1));
    
	// exlude peak
	uint32_t peak(0);
        for ( std::vector<double>::iterator it = VecFreq.begin(); it != VecFreq.end();)
        {
            // if this is the first peak in range, no peaks were found before
            if (peak < 1)
            { 
                // Grubb's Test for outliers. Factor 1,7885 was chosen exactly for 5 iterations      
                if ( std::abs(mean - *it) > ( stdev * 1.7885 ) )
	        {                    
                    ++peak;
                    VecFreq.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            // if more than one peak, abort or reinitialize
            else
            { 
	        // stop, after three attepmt to initilize frequency
	        if (InitFreqAttempt < 3)
                {
                    InitFreqAttempt++;
		    // recursion is allowed only 3 times
                    return HighPerTimer::InitHPFrequency( DelayTime );
                }
                else
                {
                    std::cerr << "TSC frequency can not be defined! Initialize process of libHPTimer was aborted!"<< std::endl;
                    throw std::exception();    
                    return;                   
                }
            }
        }        
       
        HighPerTimer::NsecPerTic = mean;
        HighPerTimer::TicsPerUsec = static_cast<uint64_t>( round  ( 1000.0 / mean ) );
	return;

    }
    else if ( TimeSource::HPET == HighPerTimer::HPTimerSource )
    {
        // TmpFreq still keeps double value of Frequency
        // needs not to call GetHPETFrequency function twice and not to open device twice
        double TmpFreq = HPETTimer::GetHPETFrequency();
        HighPerTimer::NsecPerTic = 1000.0 / TmpFreq;
        HighPerTimer::TicsPerUsec = static_cast<int64_t> ( TmpFreq );
        return;
    }
    else
    {
        HighPerTimer::NsecPerTic =  1000.0 / OSTimer::OSTimerFrequency;
        HighPerTimer::TicsPerUsec = OSTimer::OSTimerFrequency;
        return;
    }
}



// initialize the value of jiffies depends on the clock interrupt frequency of the particular hardware platform
void HighPerTimer::InitSecPerJiffy()
{
    //structures for obtaining time throuhg getcpuusage()
    struct timeval tim;
    struct rusage ru1, ru2;
    // save time value
    uint32_t CpuTime1(0), CpuTime2(0);
    uint32_t count100(0), count250(0), count300(0), count1000(0);

    // target tics of sleep time
    int64_t Target(0);

    // microseconds of sleep time, trade-off value for all kind of platforms
    uint32_t SleepTime(14500);

    // hundreds of microseconds value means system cpu usage with sleep of 14500 usec for comparing and identifying HZ
    // for HZ 100 - 0.010000 usec; for HZ 250 - 0.012000 usec; for HZ 300 - 0.013300 usec; for HZ 1000 - 0.014000 usec
    double HZ100usage(100), HZ250usage(120), HZ300usage(133), HZ1000usage(140);

    // number of seconds within one interrupt - the value of jiffies
    double HZ100Jiffies(1/100.0), HZ250Jiffies(1/250.0), HZ300Jiffies(1/300.0), HZ1000Jiffies(1/1000.0);

    uint32_t i(0), LoopCounter(10), CpuDelta(0);
    

    for ( i = 0; i < LoopCounter; i++)
    {
        // return resource usage statistics for the calling process - sleep for 14500 usec
        getrusage(RUSAGE_THREAD, &ru1);
        Target = HighPerTimer::GetTimerTics() + ( SleepTime / ( HighPerTimer::NsecPerTic / 1000LL ) ) ;
        while ( HighPerTimer::GetTimerTics() < Target )
        {
            RepNop();
        }
        getrusage(RUSAGE_THREAD, &ru2);

        // calculate seconds of system cpu usage
        tim = ru1.ru_utime;
        CpuTime1 = tim.tv_usec / 100;
        tim = ru2.ru_utime;
        CpuTime2 = tim.tv_usec / 100;
        CpuDelta = CpuTime2-CpuTime1;

        if ( CpuDelta == HZ100usage )
        {
            count100++;
            if ( count100 == 2)
            {
                HighPerTimer::HPJiffies = HZ100Jiffies;
                return;
            }
        }
        else if ( CpuDelta == HZ250usage )
        {
            count250++;
            if ( count250 == 2)
            {
                HighPerTimer::HPJiffies = HZ250Jiffies;
                return;
            }
        }
        else if ( ( CpuDelta == HZ300usage ) || ( CpuDelta == ( HZ300usage + 1 ) ) )
        {
            count300++;
            if ( count300 == 2)
            {
                HighPerTimer::HPJiffies = HZ300Jiffies;
                return;
            }
        }
        else if ( CpuDelta == HZ1000usage )
        {
            count1000++;
            if ( count1000 == 2)
            {
                HighPerTimer::HPJiffies = HZ1000Jiffies;
                return;
            }
        }
    } // end of the loop

    // in the case, when none of the HZ frequencies was detected, choose HZ 250 
    HighPerTimer::HPJiffies = HZ250Jiffies;	
    return;
}


// get tics of the appropriate timer
int64_t HighPerTimer::GetTimerTics()
{
    if ( TimeSource::TSC == HighPerTimer::HPTimerSource )
    {
        return TSCTimer::CPU_TSC();
    }
    else if ( TimeSource::HPET == HighPerTimer::HPTimerSource )
    {
        return HPETTimer::GetHPETTics();
    }
    else
    {
        return OSTimer::GetOSTimerTics();
    }
}

// get the HighPerTimer counter offset against the Unix Zero Time
void HighPerTimer::InitUnixZeroShift()
{
    // have Unix offset equall zero if it is OS Timer Source
    if ( TimeSource::OS == HighPerTimer::HPTimerSource )
    {
        HighPerTimer::UnixZeroShift = 0;
        return;
    }
           
    timespec ts;
    clock_gettime ( CLOCK_REALTIME, &ts );
    HighPerTimer::UnixZeroShift = ( static_cast<int64_t> ( ( static_cast<int64_t> ( ts.tv_sec ) * ONE_BILLION + static_cast<int64_t> ( ts.tv_nsec ) ) / HighPerTimer::NsecPerTic ) )- HighPerTimer::GetTimerTics();
           
    return;
}


// normalize the time tracking members - mSeconds, mUSeconds, mSign
void HighPerTimer::Normalize() const
{
    if ( mNormalized )
    {
        return;
    }
    // most significant bit represents the sign
    mSign =  mHPTics >> 63;
    int64_t toNSecs ( static_cast<int64_t> ( mHPTics * HighPerTimer::NsecPerTic ) );
    if ( toNSecs >> 63 )
    {
        toNSecs = std::abs ( toNSecs );
    }
    mSeconds = toNSecs / ONE_BILLION;
    mNSeconds = toNSecs % ONE_BILLION;
    mNormalized = true;
    return;
}

// changes auto timer source. Only before determining the main routine!
TimeSource HighPerTimer::SetTimerSource ( const TimeSource UserSource )
{      
    // no changed source. Do nothing
    if ( UserSource == HighPerTimer::HPTimerSource )
    {
        return HighPerTimer::HPTimerSource;
    }
    if ( TimeSource::TSC == UserSource )
    {
        if ( !TSCTimer::InitTSCTimer() )
        { // not able to initialize tsc  timer. Fallback to old time source
            return HighPerTimer::HPTimerSource;
        }
    }
    else if ( TimeSource::HPET == UserSource )
    {
        if ( !HPETTimer::InitHPETTimer() )
        { // not able to initialize hpet timer. Fallback to old time source
            return HighPerTimer::HPTimerSource;
        }
    }
    HighPerTimer::HPTimerSource = UserSource;
    HighPerTimer::InitHPFrequency( 0.02 ) ;
    HighPerTimer::InitUnixZeroShift();
    HighPerTimer::InitMaxMinHPTimer();
    return HighPerTimer::HPTimerSource;
}

// changes clock skew. Only before determining the main routine!
bool HighPerTimer::SetClockSkew ( const double DelayTime )
{      
    // only if DelayTime value is one of the allowed
    if ( ( DelayTime == 0.02 ) || ( DelayTime == 0.1 ) || ( DelayTime == 1 ) || ( DelayTime == 10 ) )
    {  
       HighPerTimer::InitHPFrequency( DelayTime ) ;
       HighPerTimer::InitUnixZeroShift();     
       return true;
    }
    else
    {
        return false;
    }
}

// equality condition.
bool HighPerTimer::operator== ( const HighPerTimer & Timer ) const
{
    if ( mHPTics == Timer.mHPTics )
    {
        return true;
    }
    return false;
}

// comparison operator for timer struct
bool HighPerTimer::operator>= ( const HighPerTimer & Timer ) const
{
    return ( mHPTics >= Timer.mHPTics );
}

// comparison operator for timer struct
bool HighPerTimer::operator<= ( const HighPerTimer & Timer ) const
{
    return ( mHPTics <= Timer.mHPTics );
}

// comparison operator for timer struct
bool HighPerTimer::operator< ( const HighPerTimer & Timer ) const
{
    return ( mHPTics < Timer.mHPTics );
}

// comparison operator for timer struct
bool HighPerTimer::operator> ( const HighPerTimer & Timer ) const
{
    return ( mHPTics > Timer.mHPTics );
}

// inequality condition for timer struct
bool HighPerTimer::operator!= ( const HighPerTimer & Timer ) const
{
    return ( mHPTics != Timer.mHPTics );
}

// assignment operator for timeval struct
const HighPerTimer & HighPerTimer::operator= ( const struct timeval & TV )
{
    *this = { static_cast<int64_t> ( TV.tv_sec ), static_cast<int64_t> ( TV.tv_usec * 1000LL ), false };
    return *this;
}

// assignment operator for timespec struct
const HighPerTimer & HighPerTimer::operator= ( const struct timespec & TS )
{
    *this = { static_cast<int64_t> ( TS.tv_sec ), static_cast<int64_t> ( TS.tv_nsec ), false };
    return *this;
}

// copy assignment
const HighPerTimer & HighPerTimer::operator= ( const HighPerTimer & Timer )
{    
    mHPTics = Timer.mHPTics;
    mNormalized = false;
    return *this;
}

// move assignment
HighPerTimer & HighPerTimer::operator= ( HighPerTimer &&  Timer )
{
    mHPTics = Timer.mHPTics;
    mNormalized = false;
    return *this ;    
}

// add tics of Timer
HighPerTimer & HighPerTimer::operator+= ( const HighPerTimer & Timer )
{
    if ( ( Timer.HPTics() > 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() - Timer.HPTics()  ) <= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Timer.HPTics() < 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() - (Timer.HPTics() ) ) >= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mHPTics += Timer.HPTics();
    mNormalized = false;
    return *this;
}

// substract tics of Timer
HighPerTimer & HighPerTimer::operator-= ( const HighPerTimer & Timer )
{
    if ( ( Timer.HPTics() > 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( Timer.HPTics() ) ) >= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Timer.HPTics() < 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() + ( Timer.HPTics() ) ) <= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mHPTics -= Timer.mHPTics;
    mNormalized = false;
    return *this;
}

// add double value of seconds to HPTimer
HighPerTimer & HighPerTimer::operator+= ( const double Seconds )
{
    if ( ( Seconds > 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() - ( static_cast<int64_t> ( Seconds /  HighPerTimer::NsecPerTic * ONE_BILLION ) ) ) <= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Seconds < 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() - ( static_cast<int64_t> ( Seconds /  HighPerTimer::NsecPerTic * ONE_BILLION ) ) ) >= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mHPTics += static_cast<int64_t> ( Seconds /  HighPerTimer::NsecPerTic * ONE_BILLION );
    mNormalized = false;
    return *this;
}

// substract double value of seconds to HPTimer
HighPerTimer & HighPerTimer::operator-= ( const double Seconds )
{
    if ( ( Seconds > 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( static_cast<int64_t> ( Seconds /  HighPerTimer::NsecPerTic * ONE_BILLION ) ) ) >= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Seconds < 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() + ( static_cast<int64_t> ( Seconds /  HighPerTimer::NsecPerTic * ONE_BILLION ) ) ) <= ( mHPTics ) ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mHPTics -= static_cast<int64_t> ( Seconds / HighPerTimer::NsecPerTic * ONE_BILLION );
    mNormalized = false;
    return *this;
}

// nil operator - returns a HighPerTimer reference with components set to zero
HighPerTimer HighPerTimer::Nil()
{
    return HighPerTimer { 0, false };
}

// add seconds
HighPerTimer & HighPerTimer::SecAdd ( const uint64_t Seconds )
{
    if ( ( HighPerTimer::HPTimer_MAX.HPTics() - ( Seconds / HighPerTimer::NsecPerTic * ONE_BILLION ) ) >= ( mHPTics ) )
    {
        mHPTics += Seconds / HighPerTimer::NsecPerTic * ONE_BILLION;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}

// add useconds
HighPerTimer & HighPerTimer::USecAdd ( const uint64_t USeconds )
{
    if ( ( HighPerTimer::HPTimer_MAX.HPTics() - ( USeconds / HighPerTimer::NsecPerTic * 1000LL ) ) >= ( mHPTics ) )
    {
        mHPTics += USeconds / HighPerTimer::NsecPerTic * 1000LL;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}

// add nseconds
HighPerTimer & HighPerTimer::NSecAdd ( const uint64_t NSeconds )
{
    if ( ( HighPerTimer::HPTimer_MAX.HPTics() - ( NSeconds / HighPerTimer::NsecPerTic ) ) >= ( mHPTics ) )
    {
        mHPTics += NSeconds / HighPerTimer::NsecPerTic;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}

// substract seconds
HighPerTimer & HighPerTimer::SecSub ( const uint64_t Seconds )
{
    if ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( ( Seconds / HighPerTimer::NsecPerTic ) * ONE_BILLION ) ) <= ( mHPTics ) )
    {
        mHPTics -= Seconds / HighPerTimer::NsecPerTic * ONE_BILLION;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}

// substract useconds, decrement seconds if to many useconds
HighPerTimer & HighPerTimer::USecSub ( const uint64_t USeconds )
{
    if ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( ( USeconds / HighPerTimer::NsecPerTic ) * 1000LL ) ) <= ( mHPTics ) )
    {
        mHPTics -= USeconds  / HighPerTimer::NsecPerTic * 1000LL;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}

// substract nseconds, decrement seconds if to many nseconds
HighPerTimer & HighPerTimer::NSecSub ( const uint64_t NSeconds )
{
    if ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( NSeconds / HighPerTimer::NsecPerTic ) ) <= ( mHPTics ) )
    {
        mHPTics -= NSeconds / HighPerTimer::NsecPerTic;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
    return *this;
}


void HighPerTimer::USecSleep ( const uint64_t USeconds ) const
{      
    mCancelled = false;  
    int64_t TargetTics (  HighPerTimer::GetTimerTics() + ( USeconds * 1000 / ( HighPerTimer::NsecPerTic ) ) );
       
    // target timer counter not shifted to unix zero
    long Counter ( 0 );
    mInterrupted = false;      
    uint64_t BusyUSeconds ( static_cast<uint64_t> (HighPerTimer::HPJiffies * 1000000 ));        
    if ( USeconds >=  BusyUSeconds )        
    {     
        std::unique_lock<std::mutex> lock (HPmutex);     
        if ( mInterrupted == true )
        {
          mCancelled = true;   
        }        
        HPcond.wait_for(lock, std::chrono::microseconds(USeconds - BusyUSeconds));  
    }
    
    while ( HighPerTimer::GetTimerTics() < TargetTics )
    {        
        // since the mInterrupted is always on the stack, we should access it not too often!
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    } 
    return;
}

// wait the amount of corresponding time in nanoseconds
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::NSecSleep ( const uint64_t NSeconds ) const
{    
    mCancelled = false; 
    int64_t TargetTics ( HighPerTimer::GetTimerTics()  + ( NSeconds / HighPerTimer::NsecPerTic ) );    
    // target timer counter not shifted to unix zero
    long Counter ( 0 );
    mInterrupted = false;
        
    uint64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );       
   
    if ( NSeconds >= BusyNSeconds )
    {         
         std::unique_lock<std::mutex> lock (HPmutex);     
         if ( mInterrupted == true )
         {
              mCancelled = true;   
         } 
         HPcond.wait_for(lock, std::chrono::microseconds( ( NSeconds - BusyNSeconds ) / 1000 ) );      
    }
    
    while ( HighPerTimer::GetTimerTics() < TargetTics )
    {
        // since the mInterrupted is always on the stack, we should access it not too often!
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
    return;
}

// wait the amount of time in tics which the corresponding timer is set to
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::TicsSleep ( const  uint64_t HPTics ) const
{
     mCancelled = false; 
    int64_t TargetTics ( HighPerTimer::GetTimerTics() + HPTics );
    // target timer counter not shifted to unix zero
    long Counter ( 0 );
    mInterrupted = false;    
    
    int64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );       
       
    if ( ( mHPTics > 0) && ( HPTics * HighPerTimer::NsecPerTic >=  BusyNSeconds ) )
    {        
        int64_t AllNSeconds ( HPTics * HighPerTimer::NsecPerTic );
        std::unique_lock<std::mutex> lock (HPmutex);      
        if ( mInterrupted == true )
        {
            mCancelled = true;   
        } 
        HPcond.wait_for(lock, std::chrono::microseconds( ( AllNSeconds - BusyNSeconds ) / 1000 ) );       
    }
    
    while ( HighPerTimer::GetTimerTics() < TargetTics )
    {
        // since the mInterrupted is always on the stack, we should access it not too often!
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
    return;
}

// wait untill CPU or  main counter time since begin of Unix era
// inline is not allowed here because of forced alignment
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::SleepTo ( const int64_t WakeHPTimer ) const
{
    mCancelled = false;
    int64_t SysNSeconds ( ( WakeHPTimer - HighPerTimer::GetTimerTics() ) * HighPerTimer::NsecPerTic );    
    
    long Counter ( 0 );
    mInterrupted = false;
    
    int64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );      
        
    if ( ( SysNSeconds > 0 ) && ( SysNSeconds >=  BusyNSeconds ) )
    {         
        std::unique_lock<std::mutex> lock (HPmutex);      
        if ( mInterrupted == true )
        {
             mCancelled = true;   
        } 
        HPcond.wait_for(lock, std::chrono::microseconds( ( SysNSeconds - BusyNSeconds ) / 1000 ));            
    }
    
    while ( HighPerTimer::GetTimerTics() < WakeHPTimer )
    {
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
}

// wait for WaitTo ticks
// inline is not allowed here because of forced alignment
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::SleepTo ( const HighPerTimer & WaitTo ) const
{
    mCancelled = false;
    int64_t SysNSeconds ( ( WaitTo.mHPTics - HighPerTimer::Now().HPTics() ) * HighPerTimer::NsecPerTic );
    long Counter ( 0 );
    mInterrupted = false;
    
    int64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );   

    // use abs(), because in case when given sleep time is too little, SysNSeconds can be negative
    if ( ( SysNSeconds > 0 ) && ( SysNSeconds >=  BusyNSeconds ) )
    {        
        std::unique_lock<std::mutex> lock (HPmutex); 
        if ( mInterrupted == true )
        {
             mCancelled = true;   
        } 
        HPcond.wait_for(lock, std::chrono::microseconds( ( SysNSeconds - BusyNSeconds ) / 1000 ) );        
    }
    
    while ( HighPerTimer::Now() < WaitTo )
    {
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
    return;
}

// sleep until 'this' time is reached
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::SleepToThis () const
{
    mCancelled = false;       
    int64_t SysNSeconds ( ( mHPTics - HighPerTimer::Now().HPTics() ) * HighPerTimer::NsecPerTic );
    long Counter ( 0 );
    mInterrupted = false;    
          
    int64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );        
       
    if ( (SysNSeconds > 0) && ( SysNSeconds >=  BusyNSeconds ) )
    {   
        std::unique_lock<std::mutex> lock (HPmutex);      
        if ( mInterrupted == true )
        {
             mCancelled = true;   
        } 
        HPcond.wait_for(lock, std::chrono::microseconds( ( SysNSeconds - BusyNSeconds ) / 1000 ) );    
    }     
     
    while ( HighPerTimer::Now().HPTics() <  mHPTics )        
    {   
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
    return;
}

// wait the amount of time the corresponding timer is set to
// NOTE: Though WakeHPTimer can be done only from another thread, no thread synchronization is done here.
// So guarantee that WakeHPTimer really wakes up the timer, can be given.
void HighPerTimer::Sleep() const
{
    mCancelled = false; 
    int64_t TargetTics ( HighPerTimer::GetTimerTics() + mHPTics );
    // target hotimer counter not shifted to unix zero
    long Counter ( 0 );
    mInterrupted = false;
    
    int64_t BusyNSeconds ( static_cast<int64_t> (HighPerTimer::HPJiffies * ONE_BILLION ) );       
       
    if ( ( mHPTics > 0) && ( mHPTics * HighPerTimer::NsecPerTic >=  BusyNSeconds ) )
    {       
        int64_t AllNSeconds = mHPTics * HighPerTimer::NsecPerTic;
        std::unique_lock<std::mutex> lock (HPmutex);      
        if ( mInterrupted == true )
        {
             mCancelled = true;   
        } 
        HPcond.wait_for(lock, std::chrono::microseconds( ( AllNSeconds - BusyNSeconds ) / 1000 ) );
    }
 
    while ( HighPerTimer::GetTimerTics() < TargetTics )
    {
        // since the mInterrupted is always on the stack, we should access it not too often!
        if ( 0 == ( ++Counter & 0x0F ) && mInterrupted )
        {
            return;
        }
        RepNop();
    }
    return;
}

// interrupt current timer from sleep. Can only be called from a different thread, accessing the same object.
// NOTE: Though it is intended to be used in a different thread from sleeps, it is for performance reason
// not thread save. So Interrupt shall be considered as an effort to wake up permaturely, but gives no garantee on this.
void HighPerTimer::Interrupt()
{
    mCancelled = true; 
    
    // avoid possible race condition    
    while ( mCancelled != true )
    {  
        mInterrupted = true;
    }
    mInterrupted = true;
    HPcond.notify_one();   
    return;
}

// get the correct time of HPtimer
HighPerTimer HighPerTimer::Now()
{
    // add UnixZeroShift time in corresponding constructor
    return HighPerTimer { HighPerTimer::GetTimerTics(), true };
}

// set the HPTimer to the correct time
// @param HPTimer is a HighPerTimer which time value shoud be set
void HighPerTimer::Now ( HighPerTimer & HPTimer )
{
    HPTimer.mNormalized = false;
    HPTimer.mHPTics = HighPerTimer::GetTimerTics() + HighPerTimer::UnixZeroShift;
}

// convert double to HPtimer. Double will be interptreted as Unix time
// @param Time is a double amount of time
// @return the converted value as HighPerTimer
HighPerTimer HighPerTimer::DtoHPTimer ( const double Time )
{
    return HighPerTimer ( static_cast <int64_t> ( Time * 1e9D  / HighPerTimer::NsecPerTic ), false );
}

// convert a HPTimer to double.
// @param HPTimer is a HPTimer which time shoud be converted
// @return converted time value as double
double HighPerTimer::HPTimertoD ( const HighPerTimer & HPTimer )
{
    return ( HPTimer.HPTics() * static_cast <double> ( HighPerTimer::NsecPerTic ) / 1e9D );
}

// convert int64 tics to double.
// @param HPTic means int64 tics which should be converted
// @return double number
double HighPerTimer::TictoD ( const int64_t HPTics )
{
    return ( HPTics * static_cast <double> ( HighPerTimer::NsecPerTic ) / 1e9D );
}

//convert int64 nanoseconds into a HighPerTimer object. NSeconds will be interptreted as Unix time
// @param NSeconds means nanoseconds which should be converted
// @return corrected HighPerTimer object
HighPerTimer HighPerTimer::NSectoHPTimer ( const int64_t NSeconds )
{
    return HighPerTimer ( NSeconds / HighPerTimer::NsecPerTic, false );
}

//convert HighPerTimer to int64 nanoseconds
// @param Timer means a HighPerTimer whose tics should be converted
// @return int64 nanoseconds value
int64_t HighPerTimer::HPTimertoNSec ( const HighPerTimer & HPTimer )
{
    HPTimer.Normalize();
    if ( HPTimer.Negative() )
    {
        return -( HPTimer.Seconds() * ONE_BILLION + HPTimer.NSeconds() );
    }
    else
    {
        return ( HPTimer.Seconds() * ONE_BILLION + HPTimer.NSeconds() );
    }
}

// set the timeval TV to the correct value of HighPerTimer
// the sign value is ignored since timeval is semantically unsigned
void HighPerTimer::SetTV ( struct timeval & TV ) const
{
    Normalize();
    TV.tv_sec = mSeconds;
    TV.tv_usec = mNSeconds / 1000LL;
}

// set the timespec TS to the correct value of HighPerTimer
// the sign value is ignored since  timespec is semantically unsigned
void HighPerTimer::SetTS ( struct timespec & TS ) const
{
    Normalize();
    TS.tv_sec = mSeconds;
    TS.tv_nsec = mNSeconds;
}

// set timer to timeval value
void HighPerTimer::SetTimer ( const timeval TV )
{
    this->SetTimer ( TV.tv_sec, TV.tv_usec * 1000LL );
    return;
}

// set timer to timespec value
void HighPerTimer::SetTimer ( const timespec TS )
{
    this->SetTimer ( TS.tv_sec, TS.tv_nsec );
    return;
}

// set timer to given seconds
void HighPerTimer::SetSeconds ( const int64_t Seconds )
{
    if ( Seconds < 0 )
    {
        this->SetTimer ( -Seconds, 0, true );
    }
    else
    {
        this->SetTimer ( Seconds, 0, false );
    }
    return;
}

// set useconds of the timer, seconds are reset
void HighPerTimer::SetUSeconds ( const int64_t USeconds )
{
    if ( USeconds < 0 )
    {
        this->SetTimer ( -USeconds / ONE_MILLION, (-USeconds  % ONE_MILLION ) * 1000LL, true );
    }
    else
    {
        this->SetTimer ( USeconds / ONE_MILLION, ( USeconds % ONE_MILLION ) * 1000LL, false );
    }
    return;
}

// set useconds of the timer, seconds are reset
void HighPerTimer::SetNSeconds ( const int64_t NSeconds )
{
    if ( NSeconds < 0 )
    {
        this->SetTimer ( -NSeconds / ONE_BILLION, NSeconds, true );
    }
    else
    {
        this->SetTimer ( NSeconds / ONE_BILLION, NSeconds, false );
    }
    return;
}

// set HPTimer tics to a given value of tics with lazy behavior of timer
// @param HPTics is tics which should be interpreted to HPtimer tics
// @exception std::out_of_range if a memory allocation failed
void HighPerTimer::SetTics ( const int64_t HPTics )
{
    if ( ( HPTics <= HPTimer_MAX.HPTics() ) || ( HPTics >= HPTimer_MAX.HPTics() ) )
    {
        mHPTics = HPTics;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    mNormalized = false;
}

// set appropriate value of the timer
// @param Seconds means the seconds part
// @param NSecodns menas the nanoseconds part
// @param Sign means the sign of the timer (true means negative value
// @exception std::out_of_range if a memory allocation failed
void HighPerTimer::SetTimer ( uint64_t Seconds, uint64_t NSeconds, bool Sign )
{
    mSeconds = Seconds;
    mNSeconds = NSeconds;
    mSign = Sign;
    // check for possible overflow according to max and min vlaue HPTimer
    if ( ( mSeconds * ONE_BILLION + mNSeconds ) > ( HighPerTimer::HPTimer_MAX.Seconds() * ONE_BILLION + HighPerTimer::HPTimer_MAX.NSeconds() ) )
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }

    if ( mSign )
    {
        // increase a little bit NsecPerTic to avoid overflow in case we calculate max value of TSC Timer source
        mHPTics = - ( static_cast<int64_t> ( ( mSeconds * ONE_BILLION + mNSeconds ) / ( HighPerTimer::NsecPerTic + ( 1/ONE_QUADRILLION ) ) ) );
    }
    else
    {
        mHPTics = static_cast<int64_t> ( ( mSeconds * ONE_BILLION + mNSeconds ) / ( HighPerTimer::NsecPerTic + ( 1/ONE_QUADRILLION ) ) );
    }
    mNormalized = true;
    return;
};

// invert the signess of the timer
HighPerTimer & HighPerTimer::InvertSign()
{
    mSign = !mSign;
    if ( mHPTics != std::numeric_limits<int64_t>::min() )
    {
        mHPTics *= -1;
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }

    return *this;
}

// accessor to the seconds part of the timer
uint64_t HighPerTimer::Seconds() const
{
    Normalize();
    return mSeconds;
}

// get microseconds through the nanosecond part
uint64_t HighPerTimer::USeconds() const
{
    Normalize();
    return mNSeconds / 1000LL;
}

// accessor to the nanoseconds part
uint64_t HighPerTimer::NSeconds() const
{
    Normalize();
    return mNSeconds;
}

// get /*string*/ of the current HighPerTimer source
std::string HighPerTimer::GetSourceString() 
{
    std::ostringstream tmpStr;
    if ( HighPerTimer::GetHPSource() == TimeSource::TSC )
    {
        tmpStr << "TSC";
	return std::string ( tmpStr.str() );  
    }
    else if ( HighPerTimer::GetHPSource() == TimeSource::HPET )
    {
        tmpStr << "HPET";
        return std::string ( tmpStr.str() );  
    } 
    else
    {
        tmpStr << "OS";
        return std::string ( tmpStr.str() );  
    }
       
}

// get the time in human readable form
// @param HPTimer_only tell, if only the value of HighPerTimer counter will be printed out.In this case, the pUnixTime parameter will be silently ignored.
// @param UnixTime forces printing the time in seconds since the begin of the Unix epoche. Defaults to true. If not set, the time in a human readable form will be printed
// @exception std::bad_alloc if a memory allocation failed
// @exception std::length_error if the maximum size of a string would be exceeded
std::string HighPerTimer::PrintTime ( bool HPTimer_only, bool UnixTime ) const
{
    std::ostringstream tmpStr;
    // if pHPTimer_only is set, print out only the value of the HPET counter
    if ( HPTimer_only == true )
    {
        tmpStr << mHPTics;
        return tmpStr.str();
    }
    // firstly, set all members!
    this->Normalize();
    // negative numbers can be printed out only in unix time format
    if ( this->Negative() )
    {
        UnixTime = true;
    }
    struct tm *t_st;
    char tmp_str[64];
    memset ( tmp_str, 0, sizeof ( tmp_str ) );
    if ( !UnixTime )
    {
        time_t u_time ( mSeconds );
        t_st = ::localtime ( ( time_t * ) & u_time );
        if ( NULL != t_st )
        {
            strftime ( tmp_str, sizeof ( tmp_str ) - 1, "%a %b %d %Y %H:%M:%S", t_st );
            tmpStr << std::dec << tmp_str << "." << std::setfill ( '0' ) << std::setw ( 9 ) << mNSeconds;
            tmpStr << std::setfill ( ' ' );
            tmpStr << "\t Timer counter: " << mHPTics;
        }
    }
    // unix time
    else
    {
        tmpStr << std::dec;
        if ( this->Negative() )
        {

            // convert from int64 to string
            std::stringstream str_stream;
            str_stream << mSeconds;
            std::string secStr =  str_stream.str();            
	    
            // size - returns a count of the number of characters in the string.
            if ( secStr.size() < 9 )
            {
                std::string fillStr ( 9 - secStr.size(), ' ' );
                tmpStr << fillStr;
            }
            tmpStr<< '-'<< secStr;
        }
        else
        {
            tmpStr.fill ( ' ' );
            tmpStr.width ( 10 );
            tmpStr << mSeconds;
        }
        tmpStr << ".";
        tmpStr.fill ( '0' );
        tmpStr.width ( 9 );
        tmpStr << mNSeconds << std::flush;
    }
    return std::string ( tmpStr.str() );
}

// system call of clock_gettime()
// return the number of nanoseconds Unix Time
int64_t HighPerTimer::GetSysTime()
{
   timespec ts;
   clock_gettime (CLOCK_REALTIME, &ts );
   return ( static_cast<int64_t> ( ts.tv_sec ) * ONE_BILLION + static_cast<int64_t> ( ts.tv_nsec ) );
}

// for using system time in logging
// return string in format <sec>.<nsec>
std::string HighPerTimer::SysNow () 
    {
        timespec ts;
        std::ostringstream tmpStr;        
        clock_gettime (CLOCK_REALTIME, &ts );
	tmpStr << ts.tv_sec;
        tmpStr << ".";
        tmpStr.fill ( '0' );
        tmpStr.width ( 9 );
        tmpStr << ts.tv_nsec << std::flush;
        return std::string (tmpStr.str());
    }


// add seconds and a timer, return result by value
HighPerTimer operator+ ( const HighPerTimer & HPTimer, const uint64_t SecOffset )
{
    if ( ( HighPerTimer::HPTimer_MAX.HPTics() - static_cast <int64_t> ( SecOffset / HighPerTimer::GetNsecPerTic() * ONE_BILLION ) ) >= ( HPTimer.HPTics() ) )
    {
        return HighPerTimer ( HPTimer.HPTics() + static_cast <int64_t> ( SecOffset / HighPerTimer::GetNsecPerTic() * ONE_BILLION ), false );
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
}

// subtraction operator for HighPerTimer
HighPerTimer operator- ( const HighPerTimer & HPTimer, const uint64_t SecOffset )
{
    if ( ( HighPerTimer::HPTimer_MIN.HPTics() + static_cast <int64_t> ( SecOffset / HighPerTimer::GetNsecPerTic() * ONE_BILLION ) ) <= ( HPTimer.HPTics() ) )
    {
        return HighPerTimer ( HPTimer.HPTics() - static_cast <int64_t> ( SecOffset / HighPerTimer::GetNsecPerTic() * ONE_BILLION ), false );
    }
    else
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
}

//////////////////////////////////////////////////////////////////
// HPTimerInitAndClean
//////////////////////////////////////////////////////////////////

// initialize timer, get the most optimal timer source and set appropriate value for TicsPerNsec, NsecPerTic, UnixZeroShift, set Min and Max value of HPTimer
// NOTE: Not use instances of this class in main routine.
HPTimerInitAndClean::HPTimerInitAndClean()
{
    // assign -1 to hpet device handle
    HPETTimer::HpetFd = -1;
    HighPerTimer::InitTimerSource();
    HighPerTimer::InitHPFrequency( 0.02 ) ;
    HighPerTimer::InitUnixZeroShift();
    HighPerTimer::InitMaxMinHPTimer();
    HighPerTimer::InitSecPerJiffy();    
}

// final clean up timer
HPTimerInitAndClean::~HPTimerInitAndClean()
{
    // check wether the hpet device was opened, and close it in this case
    if ( HPETTimer::HpetFd >= 0 )
    {
        int retval = ::close ( HPETTimer::HpetFd );
        HPETTimer::HpetFd = -1;
    }
}

} // end namespace HPTimer


