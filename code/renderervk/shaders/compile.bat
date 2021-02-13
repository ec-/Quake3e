@echo off

set bin2hex=%~dp0bin2hex.exe

echo %bin2hex%

mkdir %~dp0spirv
del /Q %~dp0spirv\shader_data.c
del /Q %~dp0spirv\vert.spv
del /Q %~dp0spirv\frag.spv

for %%f in (*.vert) do (
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -S vert -V -o %~dp0spirv\vert.spv "%%f"
    "%bin2hex%" %~dp0spirv\vert.spv +spirv\shader_data.c %%~nf_vert_spv
    del /Q %~dp0spirv\vert.spv
)

for %%f in (*.frag) do (
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -S frag -V -o %~dp0spirv\frag.spv "%%f"
    "%bin2hex%" %~dp0spirv\frag.spv +spirv\shader_data.c %%~nf_frag_spv
    del /Q %~dp0spirv\frag.spv
)
