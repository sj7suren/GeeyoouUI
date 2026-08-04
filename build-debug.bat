@echo off
rem Debug build of GeeyoouUI, in its own tree next to the Release one.
rem
rem This exists because assert() is the enforcement mechanism for contract D7
rem (core/Signal.hpp): "a slot may not destroy the object that owns the signal
rem it is running inside".  build.bat hard-codes CMAKE_BUILD_TYPE=Release, where
rem NDEBUG compiles that assert -- and every other one in the library -- out of
rem existence, so a Release-only pipeline never executes the contract at all.
rem
rem Separate build directory rather than a switch on build.bat: the two trees
rem then coexist, and "does it still pass in Release" stays one command away.
setlocal

set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJADIR=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if not exist "%VCVARS%" (
  echo [error] vcvars64.bat not found at "%VCVARS%"
  exit /b 1
)

call "%VCVARS%" >nul
set "PATH=%NINJADIR%;%PATH%"

"%CMAKE%" -G Ninja -S "%~dp0." -B "%~dp0build-debug" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%~dp0build-debug"
if errorlevel 1 exit /b 1

echo.
echo [ok] tests at %~dp0build-debug\bin\geeyoou_tests.exe
