/* @file   InterruptTestThread.cpp 
 * @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
 * @date   Apr, 2012
 * @brief  interruption of the sleeping thread with the <thread> library
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
#include <iomanip>
#include <thread>
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
};


bool TimerSleepTest::TestInterrupts ()
{
    // create the interrupt thread
    std::thread th([this]()
    {       
        HPTimer::HighPerTimer t1;
        t1.SetNow();
        for ( uint32_t i ( 0 ); i < 10; i++ )
        {
            t1.SetNow();
            this->Timer1.Interrupt();
            std::cout << " t1 interrupted at: " << t1 << std::endl;

        }
    }
                  );

    // sleeping thread
    for ( uint32_t i ( 0 ); i < 2; i++ )
    {
        Timer1.SetNow();
	// sleep for 5 sec
        Timer1.USecSleep ( 5000000 ); 
        Timer2.SetNow();
        std::cout << "Targetime: " << Timer1 << " Fire time: " << Timer2 << std::endl;
    }
    th.join();
 
    return true;
}

// test the accuracy of sleep and interrupt
int main ( int argc, char * argv[] )
{
    TimerSleepTest myTest;
    myTest.TestInterrupts ();
    return 0;
}
