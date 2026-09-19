@echo off
setlocal enabledelayedexpansion
set "SRC="
for %%f in (src\*.cpp) do set "SRC=!SRC! %%f"
g++ -std=c++17 -Wall -Wextra -O2 -o coolc.exe %SRC%
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo BUILD OK -^> coolc.exe