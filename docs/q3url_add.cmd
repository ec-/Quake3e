@echo off
set CWD=%~dp0
set CWD=%CWD:~0,-2%
set CWD=%CWD:\=\\%
set F=%TEMP%\q3a.reg
set RPATH=HKEY_CURRENT_USER\Software\Classes\q3a
rem set RPATH=HKEY_CLASSES_ROOT\q3a
echo REGEDIT4 > %F%
echo [%RPATH%] >> %F%
echo @="URL:Q3A (Quake III Arena)" >> %F%
echo "URL Protocol"="" >> %F%
echo [%RPATH%\shell\open\command] >> %F%
echo @="\"%CWD%\\quake3e.exe\" +set fs_basepath \"%CWD%\" +set fs_homepath \"%CWD%\" +connect \"%%1\"" >> %F%
regedit -s %F%
del %F%