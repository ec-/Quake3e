@echo off
attrib -H *.suo
del *.suo
del *.ncb
del *.user
rd /S /Q build
rd /S /Q output