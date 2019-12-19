@echo off
attrib -H *.suo
attrib -H .vs
del *.suo
del *.ncb
del *.user
del *.vc.db
del *.vsp
del *.psess
rd /S /Q .vs
rd /S /Q Backup
rd /S /Q build
rd /S /Q output