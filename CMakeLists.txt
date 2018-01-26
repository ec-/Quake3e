cmake_minimum_required(VERSION 3.5)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")

add_subdirectory(code)

# TODO
# mingw64 options
# CMAKE_C_FLAGS -DUSE_STATIC_GL -Wl,--stack,8388608
# CMAKE_EXE_LINKER_FLAGS --static

# vim: nowrap
