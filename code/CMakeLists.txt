#add_subdirectory(asm)
add_subdirectory(botlib)

if (NOT DEDICATED)
	add_subdirectory(client)
	add_subdirectory(jpeg-8c)
	add_subdirectory(renderer)
	#add_subdirectory(renderer2)
endif()

add_subdirectory(qcommon)
add_subdirectory(server)

if(WIN32)
add_subdirectory(win32)
else(WIN32)
add_subdirectory(unix)
endif(WIN32)
