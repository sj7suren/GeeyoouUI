@echo off
rem The whole gate, in one command: build and run the suite in ALL THREE
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
rem THAT SENTENCE APPLIES WORD FOR WORD TO THE THIRD LEG.  Neither Release nor
rem Debug checks memory safety, and neither of them can: a freed Widget's vtable
rem is still the right bits for a few microseconds, so a use-after-free finishes
rem the pass, produces the right numbers and reports PASS.  Every one of the
rem use-after-frees the two R2 security reviews found was green in both of the
rem first two legs and visible only under /fsanitize=address.  An ASan leg
rem somebody has to remember to run is not a gate either, so it is here, and a
rem red ASan leg is a red gate.
rem
rem Exit code: 0 only if all six steps below succeeded.
setlocal

set "ROOT=%~dp0"
set "RC_REL_BUILD=0"
set "RC_REL_TEST=0"
set "RC_DBG_BUILD=0"
set "RC_DBG_TEST=0"
set "RC_ASAN_BUILD=0"
set "RC_ASAN_TEST=0"

rem How many cycles the soak harness (tests/widget/test_layout_soak.cpp) runs in
rem the gate.  Short on purpose -- a gate that takes four minutes is a gate
rem people stop running -- while the nightly job sets GY_SOAK_CYCLES to
rem something six figures long, which is where a drift of one allocation per
rem cycle actually shows up.  Set here rather than left to the test's own
rem default so the gate's choice is visible in the gate.
set "GY_SOAK_CYCLES=400"

echo ==========================================================================
echo  [1/6] Release build
echo ==========================================================================
call "%ROOT%build.bat"
set "RC_REL_BUILD=%ERRORLEVEL%"
if not "%RC_REL_BUILD%"=="0" (
  set "RC_REL_TEST=skip"
  goto :debug_side
)

echo.
echo ==========================================================================
echo  [2/6] Release tests
echo ==========================================================================
"%ROOT%build\bin\geeyoou_tests.exe"
set "RC_REL_TEST=%ERRORLEVEL%"

:debug_side
echo.
echo ==========================================================================
echo  [3/6] Debug build
echo ==========================================================================
call "%ROOT%build-debug.bat"
set "RC_DBG_BUILD=%ERRORLEVEL%"
if not "%RC_DBG_BUILD%"=="0" (
  set "RC_DBG_TEST=skip"
  goto :asan_side
)

echo.
echo ==========================================================================
echo  [4/6] Debug tests
echo ==========================================================================
"%ROOT%build-debug\bin\geeyoou_tests.exe"
set "RC_DBG_TEST=%ERRORLEVEL%"

:asan_side
echo.
echo ==========================================================================
echo  [5/6] ASan build
echo ==========================================================================
call "%ROOT%build-asan.bat"
set "RC_ASAN_BUILD=%ERRORLEVEL%"
if not "%RC_ASAN_BUILD%"=="0" (
  set "RC_ASAN_TEST=skip"
  goto :summary
)

echo.
echo ==========================================================================
echo  [6/6] ASan tests
echo ==========================================================================
rem
rem THE EXIT CODE IS NOT ENOUGH HERE, and a suppression file does not help.
rem
rem This machine has a third-party IME (SogouPY.ime) injected into every
rem process, and it has a use-after-free of its own that fires while the OS
rem tears a window down.  It is not ours -- there is no GeeyoouUI frame anywhere
rem in its stack -- but by default ASan aborts on the first report, which takes
rem the whole run down after the last test has already passed.  ASan
rem suppressions cover interceptors and leak reports, not a heap-use-after-free
rem raised from inside a DLL nobody here compiled.  Nor is renaming a suite so
rem it sorts ahead of the report an answer: that hides the noise behind whatever
rem happens to run first, and the day somebody renames a case the gate silently
rem changes meaning.
rem
rem So: continue_on_error=1 turns every report into a printed report instead of
rem an abort, the whole run finishes, and the log is CLASSIFIED afterwards --
rem a report with one of OUR source paths in it is red, one without is a
rem warning.  Coarse on purpose, and coarse in the safe direction: for a real
rem defect of ours to be misfiled as third-party its entire stack would have to
rem contain no GeeyoouUI frame, and a layout bug with no layout frame is not a
rem thing.  The inverse -- a failing test printing its own file path and
rem tripping the filter -- cannot make the gate wrong either, because a failing
rem test is already red.
rem
rem detect_leaks is off because LSan does not run on Windows at all.  Leaks are
rem the soak harness's job instead: four sampled series, none of which may be
rem higher at the end of the run than after the warm-up.
set "ASAN_LOG=%ROOT%build-asan\asan-run.log"
set "ASAN_OPTIONS=continue_on_error=1:detect_leaks=0"
"%ROOT%build-asan\bin\geeyoou_tests.exe" > "%ASAN_LOG%" 2>&1
set "RC_ASAN_TEST=%ERRORLEVEL%"
type "%ASAN_LOG%"
call :classify_asan

:summary
echo.
echo ==========================================================================
echo  summary
echo ==========================================================================
call :report "Release build" "%RC_REL_BUILD%"
call :report "Release tests" "%RC_REL_TEST%"
call :report "Debug   build" "%RC_DBG_BUILD%"
call :report "Debug   tests" "%RC_DBG_TEST%"
call :report "ASan    build" "%RC_ASAN_BUILD%"
call :report "ASan    tests" "%RC_ASAN_TEST%"
echo.

if defined GY_RED (
  echo [FAIL] gate is RED. See the FAIL lines above for which side broke.
  exit /b 1
)

echo [ok] gate is GREEN: Release, Debug and ASan all built, all three suites
echo      exited 0, and no AddressSanitizer report named a GeeyoouUI frame.
exit /b 0

rem --------------------------------------------------------------------------
rem Did the ASan run report anything, and was any of it ours?  Turns
rem RC_ASAN_TEST into a failure when it was.  A subroutine rather than an inline
rem block so the nested tests need no delayed expansion, which is where batch
rem scripts go wrong.
:classify_asan
findstr /c:"ERROR: AddressSanitizer" "%ASAN_LOG%" >nul
if errorlevel 1 goto :eof
echo.
echo   --- AddressSanitizer reported at least one error; classifying ---
rem NO TRAILING BACKSLASH in any of these four.  A `\` immediately before the
rem closing quote is eaten by the CRT's argument parser as an escape for that
rem quote, findstr then receives one malformed pattern instead of four, and the
rem search silently matches nothing -- which classifies OUR OWN defects as
rem third-party noise and turns the whole leg into decoration.  Verified against
rem three hand-written logs (clean / third-party only / one GeeyoouUI frame).
findstr /i /c:"GeeyoouUI\src" /c:"GeeyoouUI\include" /c:"GeeyoouUI\tests" /c:"GeeyoouUI\examples" "%ASAN_LOG%" >nul
if errorlevel 1 (
  echo   [warn] every report is third-party ^(no GeeyoouUI frame in any stack^).
  echo          Not counted against the gate. See "%ASAN_LOG%".
  goto :eof
)
echo   [FAIL] at least one report names a GeeyoouUI source file.
set "RC_ASAN_TEST=asan"
goto :eof

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
if "%~2"=="asan" (
  echo   [FAIL] %~1 -- AddressSanitizer reported a defect in GeeyoouUI code
  set "GY_RED=1"
  goto :eof
)
echo   [FAIL] %~1 -- exit code %~2
set "GY_RED=1"
goto :eof
