SET(libSrcs 
    TimeHardware.cpp
    HighPerTimer.cpp
)

ADD_LIBRARY(
          HPTimer
          STATIC
          ${libSrcs}
)



