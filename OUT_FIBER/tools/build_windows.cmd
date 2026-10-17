@echo off
setlocal
rem Copyright (c) 2026 Alex Merqury. Source review license; see ../../LICENSE.
set "FIBER_ROOT=%~dp0.."
set "FIBER_OUT=%FIBER_ROOT%\build"
if not "%~1"=="" set "FIBER_OUT=%~1"
where cl >nul 2>nul
if errorlevel 1 (
  set "FIBER_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
if not exist "%FIBER_OUT%" mkdir "%FIBER_OUT%"
pushd "%FIBER_OUT%"
cl /nologo /std:c++17 /EHsc /O2 /fp:precise /utf-8 /W4 /MT /I"%FIBER_ROOT%\include" "%FIBER_ROOT%\src\main.cpp" "%FIBER_ROOT%\src\bank.cpp" "%FIBER_ROOT%\src\world.cpp" "%FIBER_ROOT%\src\benchmark.cpp" "%FIBER_ROOT%\tests\selftest.cpp" "%FIBER_ROOT%\src\gui_win32.cpp" /Fe:OUT_FIBER.exe /link user32.lib gdi32.lib gdiplus.lib
set "FIBER_RESULT=%errorlevel%"
popd
exit /b %FIBER_RESULT%
