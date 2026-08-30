@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cd build
"C:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" ..\SalerPromts.pro -spec win32-msvc
if errorlevel 1 exit /b 1
nmake
exit /b %ERRORLEVEL%
