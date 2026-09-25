@echo off
rem Builds Tilefish on Windows.
rem Visual Studio: run this from an "x64 Native Tools Command Prompt".
rem MinGW-w64: make sure g++ is on the PATH.
where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /O2 /std:c++17 /EHsc /Fe:tilefish.exe tilefish.cpp
) else (
  g++ -O3 -march=native -std=c++17 -pthread tilefish.cpp -o tilefish.exe
)
if exist tilefish.exe echo Done. Run tilefish.exe  (type help for commands, play to play).
