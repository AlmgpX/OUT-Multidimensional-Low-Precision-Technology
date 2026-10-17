@rem SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
@echo off
setlocal
rem Copyright (c) 2026 Alex Merqury. See ../../LICENSE.
set "NATIVE_ROOT=%~dp0..\.."
set "NATIVE_OUT=%NATIVE_ROOT%\Native\build"
if not "%~1"=="" set "NATIVE_OUT=%~1"
where cl >nul 2>nul
if errorlevel 1 (
 if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist "%NATIVE_OUT%" mkdir "%NATIVE_OUT%"
pushd "%NATIVE_OUT%"
set "GUARD_SRC=%NATIVE_ROOT%\Source\OUTGuard\Native"
set "MOTOR_SRC=%NATIVE_ROOT%\Source\OUTMotor\Native"
cl /nologo /TC /std:c11 /c /W4 /MT /I"%NATIVE_ROOT%\Native\include" "%NATIVE_ROOT%\Native\tests\c_header_check.c"
if errorlevel 1 goto failed
cl /nologo /c /std:c++17 /EHsc /O2 /fp:precise /utf-8 /W4 /MT /I"%GUARD_SRC%" /I"%MOTOR_SRC%" /I"%NATIVE_ROOT%\Native\include" "%GUARD_SRC%\guard_behavior.cpp" "%GUARD_SRC%\guard_crypto.cpp" "%GUARD_SRC%\guard_server.cpp" "%MOTOR_SRC%\OUTMotorNative.cpp" "%MOTOR_SRC%\motor_motion.cpp" "%MOTOR_SRC%\motor_navigation.cpp" "%MOTOR_SRC%\motor_view.cpp" "%MOTOR_SRC%\motor_world.cpp" "%MOTOR_SRC%\motor_combat.cpp" "%MOTOR_SRC%\motor_rts.cpp" "%NATIVE_ROOT%\Native\src\c_api.cpp"
if errorlevel 1 goto failed
lib /nologo /OUT:out_native_core.lib guard_behavior.obj guard_crypto.obj guard_server.obj OUTMotorNative.obj motor_motion.obj motor_navigation.obj motor_view.obj motor_world.obj motor_combat.obj motor_rts.obj c_api.obj
if errorlevel 1 goto failed
link /nologo /DLL /OUT:out_native.dll guard_behavior.obj guard_crypto.obj guard_server.obj OUTMotorNative.obj motor_motion.obj motor_navigation.obj motor_view.obj motor_world.obj motor_combat.obj motor_rts.obj c_api.obj bcrypt.lib
if errorlevel 1 goto failed
cl /nologo /std:c++17 /EHsc /O2 /fp:precise /utf-8 /W4 /MT /I"%GUARD_SRC%" /I"%MOTOR_SRC%" /I"%NATIVE_ROOT%\Native\include" "%NATIVE_ROOT%\Native\tests\native_checks.cpp" out_native_core.lib bcrypt.lib /Fe:out_native_checks.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
