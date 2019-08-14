@echo off

set bin2hex=%~dp0bin2hex.exe

echo %bin2hex%

for %%f in (*.vert) do (
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V "%%f"
    "%bin2hex%" vert.spv %%~nf_vert_spv > "spirv/%%~nf_vert.c"
    del vert.spv
)

for %%f in (*.frag) do (
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V "%%f"
    "%bin2hex%" frag.spv %%~nf_frag_spv > "spirv/%%~nf_frag.c"
    del frag.spv
)
