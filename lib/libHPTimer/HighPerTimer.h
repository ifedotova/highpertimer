/*
 * @file   HighPerTimer.h 
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


#ifndef _HIGHPERTIMER_H
#define _HIGHPERTIMER_H
#include "TimeHardware.h"
#include <stdexcept>
#include <atomic>

namespace HPTimer
{

/// source of timer: TSC Timer, HPET Timer or the timer, provided by the OS
enum class TimeSource
{
    TSC, HPET, OS
};

//! Main class of HighPerTimer, based on the TSC, HPET or system call of clock_gettime() 
/*!
* Seconds and nanoseconds parts are calculated only if they are explicitly accessed via accessors.
* The clock resolution is 1/tics frequency. TSC counter rate is dependent on the CPU frequency - when the CPU frequency changes,
* we can't get accurate results. Otherwise HPET device has a constant frequency, but slow access in the most cases.
*/
class HighPerTimer
{
    friend class HPTimerInitAndClean;
public:
    /// Maximum timer value
    static HighPerTimer HPTimer_MAX;
    /// Minimum timer value
    static HighPerTimer HPTimer_MIN;
    
    /**
     * change current time source, which automatically was set, to another one.
     * NOTE: Use this function only at system initialization time, and in any case
     * <b> before </b> instantiation of the first HighPerTimer object!
     * Only before the first use of an object in the main routine!
     * @param UserSource is a Timer source which should be set    
     * @return new modified Timer Source. In case this function succeeds, it is supposed to be equal UserSource
     */
    static TimeSource SetTimerSource ( const TimeSource UserSource );
    
    /**
     * change the current value of clock skew. 
     * More precisely, change the delay time during frequency initialitazion which by default is 0.02
     * The long delay needs for applications which perform long time and when clock skew value is critical.
     * NOTE: Use this function only at system initialization time, and in any case
     * <b> before </b> instantiation of the first HighPerTimer object!
     * Only before the first use of an object in the main routine!     
     * @param DelayTime is time in seconds for delay within frequency initialization process. 
     * Can be 0.02 sec, 0.1 sec, 1 sec or 10 sec. Any other value for this param is not allowed.    
     * @return true if this function succeeds, false if param is incorrect
     */    
    static bool SetClockSkew ( const double DelayTime );

    /// set the HighPerTimer counter offset against the Unix zero time - 1 January 1970
    /// NOTE: Use this function only at system initialization time, and in any case
    /// <b> before </b> instantiation of the first HighPerTimer object!
    /// Only before first use of an object in the main routine!   
    static void InitUnixZeroShift(); 
    
    /// standard ctor
    /// set tics of HPTimer equal to zero in lazy behavior,
    HighPerTimer();

    /**
     * ctor
     * @param Seconds means the seconds part
     * @param NSeconds means the nanoseconds part
     * @param Sign means the sign of the timer (true means negative value)
     * @exception std::out_of_range if a memory allocation failed
     * when one of the ints is negative, than sign must be false and when NSeconds is negative, it is allowed to use only zero Seconds. Otherwise it is an illegal initialization values.
     * Seconds and NSeconds values should be within the appropriate limits of min or max HPTimer values. Otherwise it is an HPTimer overflow.
     * HighPerTimer is extracted from the Seconds and and NSeconds parts
     */
    HighPerTimer ( int64_t Seconds, int64_t NSeconds, bool Sign = false );

    /**
     * ctor
     * @param pHighPerTimer is the HighPerTimer counter that the timer should be set
     * @param Shift tells, if the value should be shifted against the UnixZeroShift. If it is true, Unix zero shift time added to the mHPTics value
     * @exception std::out_of_range if a memory allocation failed
     * HPTics value should be within the appropriate limits of min or max HPTimer values. Otherwise it is an HPTimer overflow.
     */
    HighPerTimer ( int64_t HPTics, bool Shift = true );

    /**
     * ctor
     * @param tv is the timevalue struct
     * @exception std::out_of_range if a memory allocation failed
     */
    HighPerTimer ( const timeval & TV );

    /**
     * ctor
     * @param ts is the timespec struct
     * @exception std::out_of_range if a memory allocation failed
     */
    HighPerTimer ( const timespec & TS );

    /// default copy ctor
    /// use lazy assignment approach
    HighPerTimer ( const HighPerTimer & Timer );

    /// move ctor
    HighPerTimer ( HighPerTimer && Timer );

    /** get the tics of TSC counter of the actual CPU independently the current time source
     * @returns tics of Time stamp counter
     */
    inline static int64_t CPU_Tics()
    {
        return TSCTimer::CPU_TSC();
    };

    /** sets tsc to actual tics of TSC counter independently the current time source
     * @param tsc is a reference of Time stamp counter tics
     */
    inline static void CPU_Tics ( int64_t & tsc )
    {
        tsc = TSCTimer::CPU_TSC();
    };

    /**
     * Comparison operators for timer struct     
     */
    bool operator== ( const HighPerTimer & timer ) const;
    bool operator>= ( const HighPerTimer & timer ) const;
    bool operator<= ( const HighPerTimer & timer ) const;
    bool operator< ( const HighPerTimer & timer ) const;
    bool operator> ( const HighPerTimer & timer ) const;
    bool operator!= ( const HighPerTimer & timer ) const;

    /**
     * Type conversion operators. Assignment operators of the timer, if mNormalized is false,
     * only the mHPTics member will be set
     * @exception std::out_of_range in += and -= operators if a memory allocation failed
     */
    const HighPerTimer & operator= ( const struct timeval & TV );
    const HighPerTimer & operator= ( const struct timespec & TS );
    const HighPerTimer & operator= ( const HighPerTimer & Timer );
    HighPerTimer & operator= ( HighPerTimer && Timer );
    HighPerTimer & operator+= ( const HighPerTimer & Timer );
    HighPerTimer & operator-= ( const HighPerTimer & Timer );
    HighPerTimer & operator+= ( const double Seconds );
    HighPerTimer & operator-= ( const double Seconds );

    /// method for comparing with zero HighPerTimer values
    static HighPerTimer Nil();

    /** add Seconds to a timer
     * @param Seconds which should be added
     * @returns a corrected object
     */
    HighPerTimer & SecAdd ( const uint64_t Seconds );

    /** add USeconds to a timer
    * @param USeconds which should be added
    * @returns a corrected object
    * @exception std::out_of_range if a memory allocation failed
    */
    HighPerTimer & USecAdd ( const uint64_t USeconds );

    /** add NSeconds to a timer
    * @param NSeconds which should be added
    * @returns a corrected object
    * @exception std::out_of_range if a memory allocation failed
    */
    HighPerTimer & NSecAdd ( const uint64_t NSeconds );

    /** add given amount of clock ticks to the timer
     * @param Tics which should be added
     * @return a reference to changed object
     * @exception std::out_of_range if a memory allocation failed
     */
    inline HighPerTimer & TicAdd ( const uint64_t Tics )
    {
        if ( static_cast<int64_t> ( HighPerTimer::HPTimer_MAX.HPTics() - Tics ) >= ( mHPTics ) )
        {
             mHPTics += Tics;
        }
        else
        {
             throw  std::out_of_range ( "HPTimer overflow" );
        }
        mNormalized = false;
        return *this;
    };

    /** substract Seconds from timer
    * @param Seconds which should be substracted
    * @returns a corrected object
    * @exception std::out_of_range if a memory allocation failed
    */
    HighPerTimer & SecSub ( const uint64_t Seconds );

    /** substract USeconds from timer
     * @param USeconds which should be substracted
     * @returns a corrected object
     * @exception std::out_of_range if a memory allocation failed
     */
    HighPerTimer & USecSub ( const uint64_t USeconds );

    /** substract NSeconds from timer
     * @param NSeconds which should be substracted
     * @returns a corrected object
     * @exception std::out_of_range if a memory allocation failed
     */
    HighPerTimer & NSecSub ( const uint64_t NSeconds );

    /** substract tics from timer
     * @param Tics which should be substracted
     * @returns a reference to changed object
     * @exception std::out_of_range if a memory allocation failed
     */
    inline  HighPerTimer & TicSub ( const uint64_t Tics )
    {
        if ( static_cast<int64_t> ( HighPerTimer::HPTimer_MIN.HPTics() + Tics ) <= ( mHPTics ) )
        {
            mHPTics -= Tics;
        }
        else
        {
             throw  std::out_of_range ( "HPTimer overflow" );
        }
        mNormalized = false;
        return *this;
    };

    /** wait the amount of corresponding time in microseconds
     * @param USeconds number of microseconds for sleep
     */
    void USecSleep ( const uint64_t USeconds ) const;

    /** wait the amount of corresponding time in nanoseconds
     * @param NSeconds number of nanoseconds for sleep
     */
    void NSecSleep ( const uint64_t NSeconds ) const;

    /** wait the amount of corresponding time in tics 
     * @param HPTics number of tics for sleep
     * NOTE: Though it can be done only from another thread, no thread synchronization is done here.
     * So guarantee that it really wakes up the timer, can be given.
     */
    void TicsSleep ( const uint64_t HPTics ) const;

    /** wait untill CPU or HPET main counter ticks since begin of Unix era
     * @param WakeHPTimer value of tics, at which the method should wake up 
     * NOTE: Though it can be done only from another thread, no thread synchronization is done here.
     * So guarantee that it really wakes up the timer, can be given.
     */
    void SleepTo ( const int64_t WakeHPTimer ) const;

    /** wait for WaitTo ticks
     * @param WaitTo HighPerTimer value till which we have to sleep
     * NOTE: Though it can be done only from another thread, no thread synchronization is done here.
     * So guarantee that it really wakes up the timer, can be given.
     */
    void SleepTo ( const HighPerTimer & WaitTo ) const;

    /** wait till the value of *this
     * NOTE: Though it can be done only from another thread, no thread synchronization is done here.
     * So guarantee that it really wakes up the timer, can be given.
     */
    void SleepToThis() const;

    /** wait the period of time which the corresponding timer is set to
     * NOTE: Though it can be done only from another thread, no thread synchronization is done here.
     * So guarantee that it really wakes up the timer, can be given.
     */
    void Sleep() const;

    /// interrupt the current timer from sleep. Can only be called from a different thread, accessing the same object. 
    /// NOTE: This operation is not thread save.
    void Interrupt();

    /// get the correct time
    static HighPerTimer Now();

    /** set the HPTimer to the correct time
     * @param HPTimer is a HighPerTimer which time value shoud be set 
     */
    static void Now ( HighPerTimer & HPTimer );

    /// set timer to the correct time
    inline void SetNow()
    {
        mHPTics = HighPerTimer::GetTimerTics() + HighPerTimer::UnixZeroShift;
        mNormalized = false;
        return;
    };

    /** convert double to HPtimer. Double will be interptreted as Unix time
     * @param Time is a double amount of time
     * @return the converted value as HighPerTimer     
     */    
    static HighPerTimer DtoHPTimer ( const double Time );

    /** convert a HPTimer to double. 
     * @param HPTimer is a HPTimer which time shoud be converted
     * @return converted time value as double
     */
    static double HPTimertoD ( const HighPerTimer & HPTimer );

    /** convert int64 tics to double. 
     * @param HPTic means int64 tics which should be converted
     * @return double number
     */
    static double TictoD ( const int64_t HPTic );

    /** convert int64 nanoseconds into a HighPerTimer object. NSeconds will be interptreted as Unix time
     * @param NSeconds means nanoseconds which should be converted
     * @return corrected HighPerTimer object
     */
    static HighPerTimer NSectoHPTimer ( const int64_t NSeconds );

    /** convert HighPerTimer to int64 nanoseconds 
     * @param Timer means a HighPerTimer whose tics should be converted
     * @return int64 nanoseconds value 
     */
    static int64_t HPTimertoNSec ( const HighPerTimer & Timer );

    /** set the timeval struct
     * @param TV is a reference to a timeval struct to be set
     * NOTE: the sign value is ignored since timeval is semantically unsigned
     */
    void SetTV ( struct timeval & TV ) const;

    /** set the timespec struct
     * @param TS is a timespec pointer to the actual value of HighPerTimer
     * NOTE: the sign value is ignored since timespec is semantically unsigned
     */
    void SetTS ( struct timespec & TS ) const;

    /** set appropriate value of the timer
     * @param Seconds means the seconds part
     * @param NSecodns menas the nanoseconds part
     * @param Sign means the sign of the timer (true means negative value
     * @exception std::out_of_range if a memory allocation failed
     */
    void SetTimer ( const uint64_t Secodns, const uint64_t NSeconds, const bool Sign = false );

    /** set timeval value of the timer
     *@param TV is a timeval structure which seconds and useconds parts should equal to a HPTimer parts
     */
    void SetTimer ( const timeval TV );

    /** set timeval value of the timer
     * @param TS is a timespec structure which seconds and nseconds parts should equal to a HPTimer parts
     */
    void SetTimer ( const timespec TS );

    /** set the Seconds part of the timer
     * @param Seconds is seconds which should be interpreted to the Seconds part of timer considering the sign
     * set to zero the NSecond part, calculate tics of HPtimer.
     */
    void SetSeconds ( const int64_t Seconds );

    /** set the Nanoseconds part of the timer through microseconds
     * @param USeconds is microseconds which should be interpreted to seconds and nseconds part of HPtimer considering the sign
     * calculate tics of HPtimer.
     */
    void SetUSeconds ( const int64_t USeconds );

    /** set the Nanoseconds part of the timer
     * @param NSeconds is nanoseconds which should be interpreted to seconds and nseconds part of HPtimer considering the sign
     * calculate tics of HPtimer.
     */
    void SetNSeconds ( const int64_t NSeconds );

    /** set HPTimer tics to a given value of tics with lazy behavior of timer
     * @param HPTics is tics which should be interpreted to HPtimer tics
     * @exception std::out_of_range if a memory allocation failed
     */
    void SetTics ( const int64_t HPTics );

    /** inverts the sign of the timer value
     * @return corrected HighPerTimer object
     * @exception std::out_of_range if a memory allocation failed
     */
    HighPerTimer & InvertSign();

    /// accessor to the Seconds part, 
    /// before a valid value can be given
    /// NOTE: SLOW! Because the class must be normalized
    uint64_t Seconds() const;

    /// get microseconds through the access to the Nanoseconds part, 
    /// before a valid value can be given
    /// NOTE: SLOW! Because the class must be normalized
    uint64_t USeconds() const;

    /// accessor to the Nanoseconds part,
    /// before a valid value can be given
    /// NOTE: SLOW! because, the class must be normalized
    uint64_t NSeconds() const;

    /// accessor to the HighPerTimer counter
    inline int64_t HPTics() const
    {
        return mHPTics;
    };

    /** check a sign of timer
     * @return true, if HighPerTimer have a negatie sign
     */
    inline bool Negative() const
    {
        return ( mHPTics < 0 );
    }

    /** test if the value is nil, must be very performant
     * @return true, if HighPerTimer value is nil
    */
    inline bool IsNil() const
    {
        return ( mHPTics == 0 );
    }

    /// get HPtimer Frequency value (number of tics within one microsecond)
    inline static int64_t GetHPFrequency()
    {
        return HighPerTimer::TicsPerUsec;
    }

    /// get HPtimer time value (number of nanoseconds within one HighPerTimer period)
    inline static double GetNsecPerTic()
    {
        return HighPerTimer::NsecPerTic;
    }

    /// get HPtimer Source value
    inline static TimeSource GetHPSource()
    {
        return HighPerTimer::HPTimerSource;
    }     
        
    /// get Unix epoch offset value
    inline static int64_t GetUnixZeroShift()
    {
        return HighPerTimer::UnixZeroShift;
    }

    /// get string of current HighPerTimer source
    static std::string GetSourceString();
    
    /** get the time in human readable form
     * @param HPTimer_only tell, if only the value of HighPerTimer counter will be printed out.In this case, the pUnixTime parameter will be silently ignored.
     * @param UnixTime forces printing the time in seconds since the begin of the Unix epoche. Defaults to true. If not set, the time in a human readable form will be printed
     * @exception std::bad_alloc if a memory allocation failed
     * @exception std::length_error if the maximum size of a string would be exceeded
     */
    std::string PrintTime ( bool HPTimer_only = false, bool UnixTime = true ) const;        
    
    /** get current value of system time in nanoseconds
     * @return the number of nanoseconds 
     */
    static int64_t GetSysTime();
    
    /** get current value of system time, for using system time in logging
     * @return string in format <sec>.<nsec>
     */     
    static std::string SysNow();  
    
private:
    /// the seconds part
    mutable int64_t mSeconds;

    /// the nanoseconds part
    mutable int64_t mNSeconds;

    /// sign of the HighPerTimer, false means positive timer, true - negative
    mutable bool mSign;

    /// the main tics counter of the timer, in accordance to the time source
    int64_t mHPTics;

    /// normalized flag - if it is set, seconds, nanoseconds and sign components are syncronized with mHPTics
    mutable bool mNormalized;

    /// flag for sleep interruption
    mutable volatile bool mInterrupted; 
    
    /// flag for sleep interruption, prevent race conditions
    mutable volatile bool mCancelled;
       
    /// source for High Performance Timer: TSC, HPET or timer, provided by the OS
    static TimeSource HPTimerSource;

    /// frequency of the HighPerTimer counter, measured int increments per microsecond
    static int64_t TicsPerUsec;

    /// the reciprocal value to TicsPerUsec - number of nanoseconds within one HighPerTimer period
    static double NsecPerTic;
    
    /// timer shift (in tics) between zero of the hardware counter and Unix zero time (01.01.1970). All time systems count from Unix zero time
    static int64_t UnixZeroShift;
    
    /// the duration of one tick of the system timer interrupt, the reciprocal value to HZ - the clock interrupt frequency of the particular hardware platform
    static double HPJiffies;
    
    /// initialize the most optimal timer counter
    /// NOTE: Note this function is called by the system on static system initialization. USER SHALL NEVER ISSUE THIS CALL.
    static void InitTimerSource();

    /** set the frequency and reciprocal value NsecPerTic depends on timer source
    * @param DelayTime is time for delay in seconds for frequency initialization process. Can be 0.02 sec, 0.1 sec, 1 sec or 10 sec. 
    * Any other value for this param is not allowed.    
    * NOTE: Note this function is called by the system on static system initialization. USER SHALL NEVER ISSUE THIS CALL.
    */
    static void InitHPFrequency( const double DelayTime );
         
    /// initialize Max and Min HighPerTimer which save max and min values of Seconds, NSeconds, HPTics
    /// when HPET is timer source, the max possible value of NSecPerTic is 100 and it can be caused a possible overflow and a loss of accuracy calculating Seconds and NSeconds parts of timer
    /// so in HPET case it is more reliable to decrease limits for max and min HPTimer values 
    /// when TSC or OS clock are timer sources, the NSecPerTic value is always less than zero. So it is still safe to limit max and min HPTimer values within max and min values of int64 type 
    static void InitMaxMinHPTimer();
    
    /// initialize the value of jiffies depends on the clock interrupt frequency of the particular hardware platform
    static void InitSecPerJiffy();

    /// get current tics depends on timer source
    static int64_t GetTimerTics();

    /** Normalize the stucture - set members Seconds, NSeconds and Sign from the mHPTics value.
     * This is necessare because almost all functions will be have in default configuration in the lazy behavior
     */
    void Normalize() const;      
};

/** adding operator for HighPerTimer, using offset in seconds
 * @param Timer is a HighPerTimer summand
 * @param SecOffset is an seconds summand
 * @return timer with calculated value
 * @exception std::out_of_range if a memory allocation failed
 */
HighPerTimer operator+ ( const HighPerTimer & HPTimer, const uint64_t SecOffset );

/** subtraction operator for HighPerTimer, using offset in seconds
 * @param Timer is a HighPerTimer minuend 
 * @param SecOffset is an seconds subtrahend
 * @return timer difference with calculated value
 * @exception std::out_of_range if a memory allocation failed
 */
HighPerTimer operator- ( const HighPerTimer & HPTimer, const uint64_t SecOffset );

/** adding operator for HighPerTimer
 * @param Timer1 is the first HighPerTimer summand
 * @param Timer2 is the second HighPerTimer summand
 * @return corrected timer
 * @exception std::out_of_range if a memory allocation failed
 */
inline HighPerTimer operator+ ( const HighPerTimer & Timer1, const HighPerTimer & Timer2 )
{
    if ( ( Timer2.HPTics() > 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() - ( Timer2.HPTics() ) ) <= ( Timer1.HPTics() ) ) ) 
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Timer2.HPTics() < 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() - ( Timer2.HPTics() ) ) >= ( Timer1.HPTics() ) ) ) 
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    } 
    return HighPerTimer { ( Timer1.HPTics() + Timer2.HPTics() ), false };
}

/** subtraction operator for HighPerTimer
 * @param Timer1 is a HighPerTimer minuend 
 * @param Timer2 is a HighPerTimer substrahend
 * @return corrected timer
 * @exception std::out_of_range if a memory allocation failed
 */
inline HighPerTimer operator- ( const HighPerTimer & Timer1, const HighPerTimer & Timer2 )
{
    if ( ( Timer2.HPTics() > 0 ) && ( ( HighPerTimer::HPTimer_MIN.HPTics() + ( Timer2.HPTics() ) ) >= ( Timer1.HPTics() ) ) ) 
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
    if ( ( Timer2.HPTics() < 0 ) && ( ( HighPerTimer::HPTimer_MAX.HPTics() + ( Timer2.HPTics() ) ) <= ( Timer1.HPTics() ) ) ) 
    {
        throw  std::out_of_range ( "HPTimer overflow" );
    }
   return HighPerTimer { ( Timer1.HPTics() - Timer2.HPTics() ), false };
}

/** left shift operator
* @exception std::bad_alloc if a memory allocation failed
* @exception std::length_error if the maximum size of a string would be exceeded
*/
inline std::ostream & operator<< ( std::ostream & OStr, const HighPerTimer & Timer )
{
    OStr << Timer.PrintTime();
    return OStr;
}

//! Class for getting access to specific features of timing hardware
/*! Provide an access to some variables and functions of TimeHardware class which are closed for user.
 * AccessTimeHardware class is dedicated for detailed information about some CPU, Time Stamp Counter, HPET features 
 */
class AccessTimeHardware
{
public:
    /// exclude creating any instance of AccessTimeHardware class. Shall be used only via the friend HighPerTimer class
    AccessTimeHardware() = delete;
    
    /// return true if rdtscp assembly instruction is available 
    inline static bool IsRDTSCPSupported()
    {
        return TSCTimer::HasRDTSCPinst;
    }

    /// return true if Constant TSC is available
    inline static bool IsConstantTSC()
    {
        return TSCTimer::HasConstantTSC;
    }

    /// return true if Invariant TSC is available
    inline static bool IsInvariantTSC()
    {
        return TSCTimer::HasInvariantTSC;
    }

    /// get CPU Brand string
    inline static std::string GetBrandString()
    {
        return TSCTimer::BrandString;
    } 
    
    /// get value of hpet fail reason
    inline static HPETFail HpetFailReason()
    {
        return HPETTimer::HPETFailReason;
    }       
};


//! Class for reference counting compilation units, so things can be cleaned up at the last possible moment.
/*! Define the strict order of HighPerTimer initialization to control it, have instance in implementation HighPerTimer process 
 * NOTE: Do not use instances of this class in main routine.
 */
class HPTimerInitAndClean
{
public: 
  /// default ctor, keep the order of HighPerTimer initialization
  HPTimerInitAndClean();
  /// final clean up
  ~HPTimerInitAndClean();
};

} // end namespace HPTimer

#endif // _HIGHPERTIMER_H

