/* @file   SleepTest.cpp 
 * @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
 * @date   Apr, 2012
 * @brief  basic test of the miss time of sleeping
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

#include <iomanip>
#include <fstream>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include "HighPerTimer.h"
using namespace HPTimer;

int main(int argc, char *argv[])
{
    ///////////////////////////////////////////////////////////////
    // getting basic info
    std::cout << "Final timer source: " <<  HighPerTimer::GetSourceString() << std::endl;
    std::cout << "Frequency: " << HighPerTimer::GetHPFrequency() << std::endl;
    std::cout << "Nsec Per Tic: " << HighPerTimer::GetNsecPerTic() << std::endl;
    std::cout << "MAX HPTimer value  " << HighPerTimer::HPTimer_MAX << std::endl;
    std::cout << "MIN HPTimer value  " << HighPerTimer::HPTimer_MIN << std::endl;
   

    ///////////////////////////////////////////////////////////////
    // basic test of the miss time of sleeping
    HighPerTimer HPtimerNow1, HPtimerNow2, HPdeltaNow;
    double Ddelta;
    uint32_t LoopCounter (1000);
    double Mean(0), SqSum(0), StDev(0);
    // vector for keeping samples 
    std::vector<double> DeltaVector;
    const uint32_t ONE_MILLON (1000000), ONE_BILLON (1000000000);
    uint64_t TimeToSleep(10);
    std::cout  << "--Sleep test for " << TimeToSleep << "usec --" << std::endl;

    for (uint32_t j ( 0 ); j < 100; j++)
    {     
        for (uint32_t i ( 0 ); i < LoopCounter; i++)
        {
	    // get the current time twice with any measured operation in between
            HighPerTimer::Now(HPtimerNow1);
	    
	    // measured operation
	    //HPtimerNow1.USecSleep(TimeToSleep);  
	    usleep(TimeToSleep);
	    
	    HighPerTimer::Now(HPtimerNow2);
	    
            HPdeltaNow = HPtimerNow2 - HPtimerNow1;
            HPdeltaNow.USecSub(TimeToSleep);
	    // convert HPTimer object to double value
            Ddelta = HighPerTimer::HPTimertoD(HPdeltaNow);
            DeltaVector.push_back(Ddelta);    
        }     
    }
    
    // calculate mean values
    Mean = std::accumulate(DeltaVector.begin(), DeltaVector.end(), 0.0) / (LoopCounter * 100);
    // calculate standard deviation values
    SqSum = std::inner_product ( DeltaVector.begin(), DeltaVector.end(), DeltaVector.begin(), 0.0 );
    StDev = std::sqrt ( SqSum / DeltaVector.size() - Mean * Mean );

    std::cout << "Mean: " << std::fixed << std::setprecision(9) << Mean << " StDev: " << StDev << std::endl;
  
    return 0;
}

