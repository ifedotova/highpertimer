set ( PROJECT TimerSourceTest )
project (${PROJECT})

include_directories (
${CMAKE_CURRENT_BINARY_DIR}/
${CMAKE_CURRENT_SOURCE_DIR}/../lib/libHPTimer
) 

ADD_CUSTOM_COMMAND (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Version.h
		    # copy the version number
                    COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in
		    ${CMAKE_CURRENT_BINARY_DIR}/Version.h
		    COMMAND ${CMAKE_SOURCE_DIR}/scripts/version_inkr.sh
                    COMMAND cp ${CMAKE_CURRENT_BINARY_DIR}/Version.h
		    ${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in
		    # dependancies
                    DEPENDS ${AllSrcs};Version.h.in
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    COMMENT "UPDATING Version.h in ${CMAKE_CURRENT_BINARY_DIR}"
)

link_libraries( HPTimer )

set ( SOURCES
	 Version.h
         TimerSourceTest.cpp)
#	 InterruptTestThread.cpp)
#	 InterruptTestPthread.cpp)
       

add_executable (TimerSourceTest ${SOURCES})

ADD_DEPENDENCIES (TimerSourceTest HPTimer Version.h)
#target_link_libraries(TimerSourceTest ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libHPTimer/libHPTimer.a) 





