@echo off
rem call  "win32-setup.bat 17" to create win32-setup-fast.bat
call win32-setup-fast.bat
IF NOT EXIST build.ninja python ngen/ngen.py
ninja %*