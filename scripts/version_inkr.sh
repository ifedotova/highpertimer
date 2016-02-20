#!/bin/sh 
#
# @file   version_inkr.sh
# @author Irina Fedotova <i.fedotova@emw.hs-anhalt.de> 
# @date   Apr, 2012
# @brief  This file is only for versioning of HighPerTimer. Each make of HighPerTimer increments the build number
#   
# Copyright (C) 2012-2016,  Future Internet Lab Anhalt (FILA),
# Anhalt University of Applied Sciences, Koethen, Germany. All Rights Reserved.
# 


FILE="Version.h"
SUBSTVAR="Build"


NUMBER=`grep $SUBSTVAR ${FILE} | cut -d '=' -f 2 | cut -d ';' -f 1`

# increment the number

let NUMBER++

# replace the build number in-place

sed -i "s/$SUBSTVAR.*=.*\([0-9]\)\+;/$SUBSTVAR = $NUMBER;/" ${FILE}

echo "set $SUBSTVAR number to $NUMBER"
