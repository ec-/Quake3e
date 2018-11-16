@echo off
set F=%TEMP%\q3a.reg
echo REGEDIT4 > %F%
rem echo [-HKEY_CLASSES_ROOT\q3a] >> %F%
echo [-HKEY_CURRENT_USER\Software\Classes\q3a] >> %F%
regedit -s %F%
del %F%