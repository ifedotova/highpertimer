/* @file   InterruptTestPthread.cpp 
 * @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
 * @date   Apr, 2012
 * @brief  interruption of the sleeping thread with the <pthread.h> library
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

#include <vector>
#include <pthread.h>
#include <iostream>
#include "HighPerTimer.h"

class TimerSleepTest
{
private:
    HPTimer::HighPerTimer Timer1, Timer2;
public:

// set timers, start a background thread and interrupt from time to time timers.
// Test, if timer was interrupted. How is the interrupt accuracy?
// @return currently meanigless
    bool TestInterrupts ();

// The timer interrupt loop.
// Fire in constant intervalls an interrupt
// @param self is the accessor to the object
    static void * interrupt_loop ( void * pSelf );
};


bool TimerSleepTest::TestInterrupts ()
{
    pthread_t InterruptThread; 

    // create the interrupt thread
    pthread_create ( &InterruptThread, NULL, interrupt_loop, this );

    for ( uint32_t i ( 0 ); i < 5; i++ )
    {
        Timer1.SetNow();
	// sleep for 5 sec
        Timer1.USecSleep ( 5000000 ); 
        Timer2.SetNow();
        std::cout << "Targetime: " << Timer1 << " Fire time: " << Timer2 << std::endl;
    }

    pthread_join ( InterruptThread, NULL );
    return true;
}

// the interrupt loop
void * TimerSleepTest::interrupt_loop ( void * pSelf )
{
    TimerSleepTest * self = static_cast<TimerSleepTest *> ( pSelf );
    HPTimer::HighPerTimer t1;
    t1.SetNow();
    for ( uint32_t i ( 0 ); i < 7; i++ )
    {
        //t1.USecSleep ( 1000000 ); // 1 sec
        t1.SetNow();
        std::cout << " t1 interrupted at: " << t1 << std::endl;
        self->Timer1.Interrupt();
    }
    return NULL;
}

// test the accuracy of sleep and interrupt
int main ()
{
    TimerSleepTest myTest;
    myTest.TestInterrupts ();
    return 0;
}
