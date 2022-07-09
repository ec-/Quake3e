CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(quake3e)

OPTION(USE_SDL "" ON)
OPTION(USE_CURL "" ON)
OPTION(USE_LOCAL_HEADERS "" ON)
OPTION(USE_VULKAN "" ON)
OPTION(USE_SYSTEM_JPEG "" OFF)

SET(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules)


AUX_SOURCE_DIRECTORY(code/libjpeg JPEG_SRCS)
ADD_LIBRARY(jpeg ${JPEG_SRCS})


AUX_SOURCE_DIRECTORY(code/server SERVER_SRCS)
list(REMOVE_ITEM SERVER_SRCS code/server/sv_rankings.c)

ADD_LIBRARY(server ${SERVER_SRCS})
TARGET_LINK_LIBRARIES(server botlib qcommon)

ADD_LIBRARY(server_ded ${SERVER_SRCS})
TARGET_LINK_LIBRARIES(server_ded botlib qcommon)
TARGET_COMPILE_DEFINITIONS(server_ded PUBLIC -DDEDICATED)


AUX_SOURCE_DIRECTORY(code/qcommon QCOMMON_SRCS)
#exclude platform-dependent QVM bytecode compilers
list(FILTER QCOMMON_SRCS EXCLUDE REGEX ".*vm_[alx].*.c")

ADD_LIBRARY(qcommon ${QCOMMON_SRCS} ${VM_SRC})
TARGET_LINK_LIBRARIES(qcommon server)

ADD_LIBRARY(qcommon_ded ${QCOMMON_SRCS} ${VM_SRC})
TARGET_LINK_LIBRARIES(qcommon_ded server_ded)


AUX_SOURCE_DIRECTORY(code/botlib BOTLIB_SRCS)
ADD_LIBRARY(botlib ${BOTLIB_SRCS})
TARGET_COMPILE_DEFINITIONS(botlib PRIVATE -DBOTLIB)


AUX_SOURCE_DIRECTORY(code/client CLIENT_SRCS)
IF(NOT USE_CURL)
    list(REMOVE_ITEM CLIENT_SRCS code/client/cl_curl.c)
ENDIF()

ADD_LIBRARY(client ${CLIENT_SRCS})
TARGET_LINK_LIBRARIES(client qcommon jpeg)

IF(USE_SYSTEM_JPEG)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE -DUSE_SYSTEM_JPEG)
ENDIF()

IF(USE_VULKAN)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE -DUSE_VULKAN_API)
ENDIF()


AUX_SOURCE_DIRECTORY(code/renderercommon RENDERER_COMMON_SRCS)
ADD_LIBRARY(renderer_common ${RENDERER_COMMON_SRCS})
TARGET_LINK_LIBRARIES(renderer_common qcommon)


AUX_SOURCE_DIRECTORY(code/renderer RENDERER_SRCS)
ADD_LIBRARY(renderer ${RENDERER_SRCS})
TARGET_LINK_LIBRARIES(renderer renderer_common)


AUX_SOURCE_DIRECTORY(code/renderervk RENDERER_VK_SRCS)
ADD_LIBRARY(renderer_vk ${RENDERER_VK_SRCS})
TARGET_LINK_LIBRARIES(renderer_vk renderer_common)


IF(USE_SDL)
	AUX_SOURCE_DIRECTORY(code/sdl SDL_SRCS)
	ADD_LIBRARY(qsdl ${SDL_SRCS})
	find_package(SDL2 REQUIRED)
	TARGET_LINK_LIBRARIES(qsdl SDL2::Main)
	IF(USE_VULKAN)
		TARGET_COMPILE_DEFINITIONS(qsdl PRIVATE -DUSE_VULKAN_API)
	ENDIF()
ENDIF()

IF(UNIX)
	SET (Q3_SRCS
		code/unix/unix_main.c
		code/unix/unix_shared.c
		code/unix/linux_signals.c)

	IF (NOT USE_SDL)
		find_package (X11 REQUIRED)
		SET(Q3_UI_SRCS
			code/unix/linux_glimp.c
			code/unix/linux_qgl.c
			code/unix/linux_snd.c
			code/unix/x11_dga.c
			code/unix/x11_randr.c
			code/unix/x11_vidmode.c)
		IF(USE_VULKAN)
			list(APPEND Q3_UI_SRCS code/unix/linux_qvk.c)
		ENDIF()
	ENDIF()

	IF(APPLE)
		SET(EXE_TYPE MACOSX_BUNDLE)
		list(APPEND Q3_SRCS code/unix/quake3_flat.icns)
		set_source_files_properties(code/unix/quake3_flat.icns PROPERTIES
			MACOSX_PACKAGE_LOCATION Resources)
		SET(MACOSX_BUNDLE_ICON_FILE quake3_flat.icns)
	ENDIF()
ELSEIF(WIN32)
	SET(EXE_TYPE WIN32)
	SET(Q3_SRCS
		code/win32/win_main.c
		code/win32/win_shared.c
		code/win32/win_syscon.c
		code/win32/win_resource.rc)
	SET(Q3_UI_SRCS
		code/win32/win_gamma.c
		code/win32/win_glimp.c
		code/win32/win_input.c
		code/win32/win_minimize.c
		code/win32/win_qgl.c
		code/win32/win_snd.c
		code/win32/win_wndproc.c)
	IF(USE_VULKAN)
		list(APPEND Q3_UI_SRCS code/win32/win_qvk.c)
	ENDIF()
ENDIF(UNIX)

IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64|x86|i*86)
	SET(VM_SRCS code/qcommon/vm_x86.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES aarch64|arm64)
	SET(VM_SRCS code/qcommon/vm_aarch64.c)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES arm*)
	SET(VM_SRCS code/qcommon/vm_armv7l.c)
ELSE()
	TARGET_COMPILE_DEFINITIONS(qcommon PRIVATE -DNO_VM_COMPILED)
ENDIF()

#client executable

ADD_EXECUTABLE(quake3e ${EXE_TYPE} ${VM_SRCS} ${Q3_SRCS} ${Q3_UI_SRCS})

TARGET_LINK_LIBRARIES(quake3e client botlib)

#TARGET_LINK_OPTIONS(quake3e PRIVATE -fno-pie) #since version 3.13

IF(USE_SDL)
	TARGET_LINK_LIBRARIES(quake3e qsdl)
ELSEIF(UNIX)
	TARGET_LINK_LIBRARIES(quake3e ${X11_LIBRARIES})
ENDIF()

IF(USE_VULKAN)
	TARGET_COMPILE_DEFINITIONS(quake3e PRIVATE -DUSE_VULKAN_API)
	TARGET_LINK_LIBRARIES(quake3e renderer_vk)
ELSE()
	TARGET_LINK_LIBRARIES(quake3e renderer)
ENDIF(USE_VULKAN)

#dedicated server executable

ADD_EXECUTABLE(quake3e.ded ${EXE_TYPE} ${VM_SRCS} ${Q3_SRCS})
TARGET_LINK_LIBRARIES(quake3e.ded qcommon_ded botlib)
#TARGET_LINK_OPTIONS(quake3e.ded PRIVATE -fno-pie) #since version 3.13

IF(WIN32)
	TARGET_LINK_LIBRARIES(quake3e winmm comctl32 ws2_32)
	TARGET_LINK_LIBRARIES(quake3e.ded winmm comctl32 ws2_32)
ELSE()
	TARGET_LINK_LIBRARIES(quake3e m ${CMAKE_DL_LIBS})
	TARGET_LINK_LIBRARIES(quake3e.ded m ${CMAKE_DL_LIBS})
ENDIF()
