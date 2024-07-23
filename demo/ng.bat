call win32-setup.bat 17
IF NOT EXIST build.ninja python ngen/ngen.py
win32-ninja.bat %*