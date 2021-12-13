cmake_minimum_required(VERSION 3.10)
project(quake3e)


option(USE_SDL "" ON)
option(USE_CURL "" ON)
option(USE_LOCAL_HEADERS "" ON)
option(USE_VULKAN "" OFF)
option(USE_SYSTEM_JPEG "" OFF)

option(USE_RENDERER_DLOPEN "" OFF)


set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules)

aux_source_directory(code/libjpeg JPEG_SRCS)
add_library(jpeg ${JPEG_SRCS})


aux_source_directory(code/server SERVER_SRCS)
list(REMOVE_ITEM SERVER_SRCS code/server/sv_rankings.c)
add_library(server ${SERVER_SRCS})
add_library(server_ded ${SERVER_SRCS})
target_compile_definitions(server_ded PUBLIC -DDEDICATED)


aux_source_directory(code/qcommon QCOMMON_SRCS)
list(FILTER QCOMMON_SRCS EXCLUDE REGEX ".*vm_[alx].*.c")


add_library(qcommon ${QCOMMON_SRCS} ${VM_SRC})
target_link_libraries(qcommon PRIVATE server)
add_library(qcommon_ded ${QCOMMON_SRCS} ${VM_SRC})
target_link_libraries(qcommon_ded PUBLIC server_ded)


aux_source_directory(code/botlib BOTLIB_SRCS)
add_library(botlib ${BOTLIB_SRCS})
target_compile_definitions(botlib PRIVATE -DBOTLIB)


aux_source_directory(code/client CLIENT_SRCS)
if(NOT USE_CURL)
    list(REMOVE_ITEM CLIENT_SRCS code/client/cl_curl.c)
endif(NOT USE_CURL)
add_library(client ${CLIENT_SRCS})
target_link_libraries(client qcommon jpeg)
if(USE_SYSTEM_JPEG)
    target_compile_definitions(client PRIVATE -DUSE_SYSTEM_JPEG)
endif(USE_SYSTEM_JPEG)
if(USE_VULKAN)
    target_compile_definitions(client PRIVATE -DUSE_VULKAN_API)
endif(USE_VULKAN)



aux_source_directory(code/renderercommon RENDERER_COMMON_SRCS)
add_library(renderer_common ${RENDERER_COMMON_SRCS})


aux_source_directory(code/renderer RENDERER_SRCS)
add_library(renderer ${RENDERER_SRCS})
target_link_libraries(renderer renderer_common)


aux_source_directory(code/renderervk RENDERER_VK_SRCS)
add_library(renderer_vk ${RENDERER_VK_SRCS})
target_link_libraries(renderer_vk renderer_common)


if(USE_SDL)
    aux_source_directory(code/sdl SDL_SRCS)
    add_library(qsdl ${SDL_SRCS})
    find_package(SDL2 REQUIRED)
    target_link_libraries(qsdl SDL2::Main)
    if(USE_VULKAN)
        target_compile_definitions(qsdl PRIVATE -DUSE_VULKAN_API)
    endif(USE_VULKAN)

endif()

if(UNIX)
    set(Q3_SRCS
            code/unix/unix_main.c
            code/unix/unix_shared.c
            code/unix/linux_signals.c)

    if (NOT USE_SDL)
        set(Q3_UI_SRCS
                code/unix/linux_glimp.c
                code/unix/linux_qgl.c
                code/unix/linux_snd.c
                code/unix/x11_dga.c
                code/unix/x11_randr.c
                code/unix/x11_vidmode.c)
        if(USE_VULKAN_API)
            list(APPEND Q3_UI_SRCS code/unix/linux_qvk.c)
        endif(USE_VULKAN_API)
    endif(NOT USE_SDL)

    if(APPLE)
        set(EXE_TYPE MACOSX_BUNDLE)
        list(APPEND Q3_SRCS code/unix/quake3_flat.icns)
        set_source_files_properties(code/unix/quake3_flat.icns PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources)
        set(MACOSX_BUNDLE_ICON_FILE quake3_flat.icns)
    endif()
elseif(WIN32)
    set(EXE_TYPE WIN32)
    set(Q3_SRCS
            code/win32/win_main.c
            code/win32/win_shared.c
            code/win32/win_syscon.c
            code/win32/win_resource.rc)
    if (NOT USE_SDL)
        set(Q3_UI_SRCS
                code/win32/win_gamma.c
                code/win32/win_glimp.c
                code/win32/win_input.c
                code/win32/win_minimize.c
                code/win32/win_qgl.c
                code/win32/win_snd.c
                code/win32/win_wndproc.c)
        if(USE_VULKAN_API)
            list(APPEND Q3_UI_SRCS code/win32/win_qvk.c)
        endif(USE_VULKAN_API)
    endif(NOT USE_SDL)
endif(UNIX)


if(CMAKE_SYSTEM_PROCESSOR MATCHES AMD|x86)
    set(VM_SRCS code/qcommon/vm_x86.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES aarch|arm64)
    set(VM_SRCS code/qcommon/vm_aarch64.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES arm)
    set(VM_SRCS code/qcommon/vm_armv7l.c)
else()
    target_compile_definitions(qcommon PRIVATE -DNO_VM_COMPILED)
endif()


add_executable(quake3e ${EXE_TYPE} ${VM_SRCS} ${Q3_SRCS} ${Q3_UI_SRCS})
target_link_libraries(quake3e client botlib)

if(USE_SDL)
    target_link_libraries(quake3e qsdl)
endif(USE_SDL)

if(USE_VULKAN)
    target_link_libraries(quake3e renderer_vk)
    target_compile_definitions(quake3e PRIVATE -DUSE_VULKAN_API)
else()
    target_link_libraries(quake3e renderer)
endif(USE_VULKAN)

add_executable(quake3e.ded ${EXE_TYPE} ${VM_SRCS} ${Q3_SRCS})
target_link_libraries(quake3e.ded qcommon_ded botlib)

if(WIN32)
    target_link_libraries(quake3e winmm comctl32 ws2_32)
    target_link_libraries(quake3e-server winmm comctl32 ws2_32)
endif(WIN32)
