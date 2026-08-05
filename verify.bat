@echo off
rem The whole gate, in one command: build and run the suite in BOTH
rem configurations, then say plainly which side is red.
rem
rem Release and Debug are not two flavours of the same run.  NDEBUG decides
rem whether contract D7's assert exists at all (core/Signal.hpp), so a green
rem Release run says nothing about that assert; and _ITERATOR_DEBUG_LEVEL makes
rem MSVC's STL allocate a proxy per container, so a green Debug run says nothing
rem about the allocation counts in tests/core/test_signal.cpp.  Each build
rem checks something the other structurally cannot.  Relying on somebody
rem remembering to run the second one is not a gate, which is why this file
rem exists and why CI should call it rather than build.bat.
rem
rem Exit code: 0 only if all four steps below succeeded.
setlocal

set "ROOT=%~dp0"
set "RC_REL_BUILD=0"
set "RC_REL_TEST=0"
set "RC_DBG_BUILD=0"
set "RC_DBG_TEST=0"

echo ==========================================================================
echo  [1/4] Release build
echo ==========================================================================
call "%ROOT%build.bat"
set "RC_REL_BUILD=%ERRORLEVEL%"
if not "%RC_REL_BUILD%"=="0" (
  set "RC_REL_TEST=skip"
  goto :debug_side
)

echo.
echo ==========================================================================
echo  [2/4] Release tests
echo ==========================================================================
"%ROOT%build\bin\geeyoou_tests.exe"
set "RC_REL_TEST=%ERRORLEVEL%"

:debug_side
echo.
echo ==========================================================================
echo  [3/4] Debug build
echo ==========================================================================
call "%ROOT%build-debug.bat"
set "RC_DBG_BUILD=%ERRORLEVEL%"
if not "%RC_DBG_BUILD%"=="0" (
  set "RC_DBG_TEST=skip"
  goto :summary
)

echo.
echo ==========================================================================
echo  [4/4] Debug tests
echo ==========================================================================
"%ROOT%build-debug\bin\geeyoou_tests.exe"
set "RC_DBG_TEST=%ERRORLEVEL%"

:summary
echo.
echo ==========================================================================
echo  summary
echo ==========================================================================
call :report "Release build" "%RC_REL_BUILD%"
call :report "Release tests" "%RC_REL_TEST%"
call :report "Debug   build" "%RC_DBG_BUILD%"
call :report "Debug   tests" "%RC_DBG_TEST%"
echo.

if defined GY_RED (
  echo [FAIL] gate is RED. See the FAIL lines above for which side broke.
  exit /b 1
)

echo [ok] gate is GREEN: Release and Debug both built and both suites exited 0.
exit /b 0

rem --------------------------------------------------------------------------
rem One line of the summary.  A step that never ran because the build ahead of
rem it failed is reported as a failure too -- "not run" must never read as
rem "passed".
:report
if "%~2"=="0" (
  echo   [ ok ] %~1
  goto :eof
)
if "%~2"=="skip" (
  echo   [FAIL] %~1 -- not run, the build before it failed
  set "GY_RED=1"
  goto :eof
)
echo   [FAIL] %~1 -- exit code %~2
set "GY_RED=1"
goto :eof
