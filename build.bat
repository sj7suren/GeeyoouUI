@echo off
rem Build GeeyoouUI with the CMake/Ninja/MSVC that ship inside Visual Studio.
rem Nothing needs to be on PATH beforehand.
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

"%CMAKE%" -G Ninja -S "%~dp0." -B "%~dp0build" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%~dp0build"
if errorlevel 1 exit /b 1

echo.
echo [ok] binary at %~dp0build\bin\showcase.exe
