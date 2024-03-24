#! /bin/bash
source ./build.sh
build vs2017/Win32 "Visual Studio 17 2022" -A Win32 -T v141
build vs2017/x64   "Visual Studio 17 2022" -A x64   -T v141
build vs2017/ARM64 "Visual Studio 17 2022" -A ARM64 -T v141
