set ( PROJECT TimerSourceTest )
project (${PROJECT})

include_directories (
${CMAKE_CURRENT_BINARY_DIR}/
${CMAKE_CURRENT_SOURCE_DIR}/../lib/libHPTimer
) 

ADD_CUSTOM_COMMAND (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Version.h
		    # copy the version number
        COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/Version.h
		    COMMAND ${CMAKE_SOURCE_DIR}/scripts/version_inkr.sh 
        COMMAND cp ${CMAKE_CURRENT_BINARY_DIR}/Version.h ${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in
		    # dependancies
        DEPENDS ${AllSrcs};Version.h.in
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "UPDATING Version.h in ${CMAKE_CURRENT_BINARY_DIR}"
)

set_target_properties ( HPTimer PROPERTIES IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libHPTimer/libHPTimer.a )

link_libraries( HPTimer )

file( GLOB APP_SOURCES ${CMAKE_SOURCE_DIR}/app/*.cpp )

foreach( testSourceFile ${APP_SOURCES} )
	get_filename_component( testExecutableName ${testSourceFile} NAME )
	message( "Adding executable " ${testExecutableName} )
	add_executable( ${testExecutableName} ${testSourceFile} )
	target_link_libraries( ${testExecutableName} HPTimer )
endforeach( testSourceFile ${APP_SOURCES} )
