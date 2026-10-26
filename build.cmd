@echo off
setlocal
cd /d "%~dp0"
if not exist ".cache\temp" mkdir ".cache\temp"
set "TEMP=%~dp0.cache\temp"
set "TMP=%~dp0.cache\temp"
set "TASK_BUILD_DIR=.cache/build"
if not "%~1"=="" set "TASK_BUILD_DIR=.cache/%~1"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "TASK_CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "TASK_NINJA=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
"%TASK_CMAKE%" -S . -B "%TASK_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%TASK_NINJA%"
if errorlevel 1 exit /b 1
if "%~2"=="" (
    "%TASK_CMAKE%" --build "%TASK_BUILD_DIR%"
) else (
    "%TASK_CMAKE%" --build "%TASK_BUILD_DIR%" --target "%~2"
)
exit /b %errorlevel%
