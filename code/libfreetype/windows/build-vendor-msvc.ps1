# Rebuild FreeType static libs for MSVC with /MT|/MTd (matches quake3e RuntimeLibrary).
# Requires: VS Developer environment (run via VsDevCmd), cmake, tar.
$ErrorActionPreference = "Stop"

$Root = "C:\git\q3e"
$FtWin = Join-Path $Root "code\libfreetype\windows"
$VendorTmp = Join-Path $env:TEMP "q3e-vendor"
$FtTag = "VER-2-13-3"
$FtDir = "freetype-$FtTag"
$Tar = Join-Path $VendorTmp "freetype-$FtTag.tar.gz"
$Cmake = "C:\Program Files\CMake\bin\cmake.exe"

if (-not (Test-Path $Cmake)) { throw "cmake not found: $Cmake" }
if (-not (Test-Path $Tar)) { throw "Missing $Tar - download FreeType $FtTag tarball first" }

New-Item -ItemType Directory -Force -Path $VendorTmp | Out-Null
Set-Location $VendorTmp

if (-not (Test-Path (Join-Path $VendorTmp $FtDir))) {
  tar xzf $Tar
}

$FtCommon = @(
  "-DBUILD_SHARED_LIBS=OFF",
  "-DFT_DISABLE_ZLIB=TRUE",
  "-DFT_DISABLE_BZIP2=TRUE",
  "-DFT_DISABLE_PNG=TRUE",
  "-DFT_DISABLE_HARFBUZZ=TRUE",
  "-DFT_DISABLE_BROTLI=TRUE",
  "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
  '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>',
  "-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG",
  "-DCMAKE_C_FLAGS_DEBUG=/MTd /Zi /Ob0 /Od /RTC1"
)

function Build-FtArch([string]$Arch, [string]$OutDir, [string]$LibDir) {
  $BuildDir = Join-Path $VendorTmp "ft-msvc-$Arch"
  if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
  if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
  New-Item -ItemType Directory -Force -Path $BuildDir, $OutDir, $LibDir | Out-Null

  $Src = Join-Path $VendorTmp $FtDir
  Push-Location $BuildDir
  try {
    & $Cmake $Src -A $Arch "-DCMAKE_INSTALL_PREFIX=$OutDir" @FtCommon
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for $Arch" }

    # Force /MD -> /MT in generated files if FreeType ignores runtime setting
    Get-ChildItem -Recurse -Include *.vcxproj,CMakeCache.txt,flags.make,*.cmake |
      ForEach-Object {
        $t = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $t) { return }
        $n = $t -replace '/MDd', '/MTd' -replace '/MD', '/MT'
        if ($n -ne $t) { Set-Content -Path $_.FullName -Value $n -NoNewline }
      }

    & $Cmake --build . --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Release build failed for $Arch" }
    & $Cmake --install . --config Release
    if ($LASTEXITCODE -ne 0) { throw "Release install failed for $Arch" }

    & $Cmake --build . --config Debug --parallel
    if ($LASTEXITCODE -ne 0) { throw "Debug build failed for $Arch" }

    $dbg = Get-ChildItem -Path $BuildDir -Recurse -Filter "freetype*.lib" |
      Where-Object { $_.FullName -match '\\Debug\\' } |
      Select-Object -First 1
    if (-not $dbg) { throw "Debug freetype lib not found for $Arch" }

    Copy-Item -Force (Join-Path $OutDir "lib\freetype.lib") (Join-Path $LibDir "freetype_a.lib")
    Copy-Item -Force $dbg.FullName (Join-Path $LibDir "freetype_a_debug.lib")
  }
  finally {
    Pop-Location
  }
}

$Out64 = Join-Path $VendorTmp "out64-msvc"
$Out32 = Join-Path $VendorTmp "out32-msvc"
Build-FtArch -Arch "x64" -OutDir $Out64 -LibDir (Join-Path $FtWin "vs2017\lib64")
Build-FtArch -Arch "Win32" -OutDir $Out32 -LibDir (Join-Path $FtWin "vs2017\lib32")

# Refresh headers from x64 install
$Inc = Join-Path $FtWin "include"
if (Test-Path $Inc) { Remove-Item -Recurse -Force $Inc }
New-Item -ItemType Directory -Force -Path $Inc | Out-Null
Copy-Item -Recurse -Force (Join-Path $Out64 "include\*") $Inc

Write-Host "Installed:"
Get-ChildItem (Join-Path $FtWin "vs2017") -Recurse -Filter "*.lib" | ForEach-Object {
  "{0}  {1}" -f $_.Length, $_.FullName
}
