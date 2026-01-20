@echo off
setlocal enabledelayedexpansion

cmake -S . -B build -A x64
if errorlevel 1 exit /b %errorlevel%

cmake --build build --config Release
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 exit /b %errorlevel%

exit /b 0
