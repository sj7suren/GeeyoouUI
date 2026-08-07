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
rem AND YOUR TEST MUST LET THE READ SURVIVE TO THIS LEG.  A green ASan run says
rem nothing about a load the optimiser deleted: `(void)w->geometry();` has no
rem consumer, /O2 removes it, and the use-after-free is then unobservable HERE
rem while the case still fails on its own flag assertions.  That is exactly what
rem E14 measured -- failing cases, zero reports -- so every read of a
rem possibly-freed object in a test must be CONSUMED by a CHECK.  "The case went
rem red" and "ASan went red" are two independent signals; only the second one is
rem evidence about memory safety.  See build-asan.bat's header for the full
rem account.
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
call :lint_doors
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
rem an abort, the whole run finishes, and the log is CLASSIFIED afterwards, by
rem tools\classify-asan.ps1.
rem
rem That classification used to be one findstr: does one of OUR source paths
rem appear ANYWHERE in the log?  It had to go, because the IME case above is
rem exactly the case it gets wrong.  Our Window::~Window IS on SogouPY's stack
rem -- it is the frame that called DestroyWindow -- so their report contains our
rem path, the findstr matched, and the gate went red over somebody else's bug.
rem A gate that reddens at random is worse than no gate: the team learns to
rem click past it, and then it is not watching the real defects either.
rem
rem The replacement asks WHO IS RESPONSIBLE instead of WHO APPEARS.  For the
rem use-after-free family it looks at the innermost non-runtime frame of the use
rem site and of the free site; ours at either end is red, third-party at both is
rem [known].  For every other report kind it keeps the old broad rule verbatim.
rem The full argument, including what it deliberately does not catch, is in the
rem header of tools\classify-asan.ps1 -- read that before loosening anything.
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
echo      exited 0, and no AddressSanitizer report was attributable to GeeyoouUI.
exit /b 0

rem --------------------------------------------------------------------------
rem DOOR COVERAGE (section 11.9 of docs\iterations\02-layout-engine.md).
rem
rem The three legs below check BEHAVIOUR.  None of them can check the property
rem this family keeps losing, which is a property of the SOURCE: that every
rem statement handing control to application code, with member access after it,
rem either carries a cursor or is written down in section 11.4 with a reason, a
rem grade and a round.  Six recurrences, and every one was found by a person
rem re-reading code a person had already reviewed.  layoutRect() -- a door on
rem the every-frame layout path -- survived two full enumerations because A
rem VIRTUAL CALL HAS NO NAME TO GREP, and it was found by a human in the end.
rem
rem So this runs before the build, on the sources, and a candidate with no home
rem makes the gate red.
rem
rem WHY IT IS INSIDE STEP [1/6] AND NOT A STEP [7].  The six-banner shape and
rem the six-row summary are a contract other things read; the classifier
rem self-test in step [6/6] already established how to add a check without
rem adding a step.  A source check belongs to the step that turns sources into
rem a binary, so it goes here, prints its own [ ok ] / [FAIL] line, and sets
rem GY_RED directly.  The summary table is untouched, and the final verdict
rem already tells the reader to look for FAIL lines above.
rem
rem IT DOES NOT ABORT THE BUILD.  An unarchived candidate means somebody wrote a
rem door and did not register it; it does not mean the tree fails to compile,
rem and throwing away the other five steps' evidence to say so would trade a
rem real signal for a louder one.  The gate still goes red.
rem
rem AND THE LINT IS ASKED TO PROVE ITSELF FIRST, for the same reason the ASan
rem classifier is: it is the only thing standing between an unguarded door and a
rem release, and a checker that fails its own fixtures has no verdict worth
rem reading.  A self-test failure does NOT fall through to the lint.
rem
rem Budget: self-test ~6.5s, lint ~2s.  If either creeps past ~15s, fix it
rem there -- do not delete this call.  Both run their tables in one process.
:lint_doors
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive ^
  -ExecutionPolicy Bypass -File "%ROOT%tools\test-lint-door-coverage.ps1" -Quiet
set "RC_LINT_SELFTEST=%ERRORLEVEL%"
if not "%RC_LINT_SELFTEST%"=="0" (
  echo   [FAIL] door-coverage lint self-test FAILED, so the lint was NOT run.
  echo          The failing case and the reason it exists are printed above.
  echo          Fix tools\lint-door-coverage.ps1, or the fixture that caught it.
  set "GY_RED=1"
  goto :eof
)

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive ^
  -ExecutionPolicy Bypass -File "%ROOT%tools\lint-door-coverage.ps1" -Quiet
set "RC_LINT=%ERRORLEVEL%"
if "%RC_LINT%"=="0" (
  echo   [ ok ] door coverage: every candidate is guarded or archived in section 11.4.
  goto :eof
)
if "%RC_LINT%"=="1" (
  echo   [FAIL] door coverage: a candidate has no home. Guard the frame, or register
  echo          it in section 11.4 of docs\iterations\02-layout-engine.md with a
  echo          reason, a grade and a round. Do NOT lower a grade to silence this.
  set "GY_RED=1"
  goto :eof
)
echo   [FAIL] the door-coverage lint reached no verdict ^(exit %RC_LINT%^).
echo          Counted as UNCOVERED on purpose: fail-closed, same rule as the ASan
echo          classifier below.
set "GY_RED=1"
goto :eof

rem --------------------------------------------------------------------------
rem Did the ASan run report anything, and was any of it OURS?  Turns
rem RC_ASAN_TEST into a failure when it was.  A subroutine rather than an inline
rem block so the nested tests need no delayed expansion, which is where batch
rem scripts go wrong.
rem
rem The verdict is the classifier's EXIT CODE, and the mapping below is
rem exhaustive on purpose:
rem
rem     0  nothing to classify                    -> leg unchanged
rem     2  reports, all third-party               -> [known], leg unchanged
rem     1  at least one report is ours            -> RED
rem     anything else                             -> RED
rem
rem That last line is the whole safety property.  Exit 3 is the script's own
rem "I could not parse this", 9009 is "powershell is not on PATH", and a syntax
rem error in the .ps1 gives something else again -- and every one of them lands
rem in the red branch.  A classifier that fails open is worse than no
rem classifier, because it looks like one, and the five use-after-frees the R2
rem reviews found were all green in the other two legs.  This leg is the only
rem thing between that class of defect and a release.
rem
rem Absolute path to powershell.exe rather than bare `powershell` so a PATH
rem somebody else edited cannot silently pick up a different interpreter; if it
rem is missing, cmd returns 9009 and, per the table above, the gate goes red.
rem
rem Note also what is NOT passed here: %ROOT% ends in a backslash, and "%ROOT%"
rem as an argument would have its closing quote eaten by the CRT's argument
rem parser -- the same trailing-backslash trap that once left this very
rem subroutine's findstr matching nothing at all and the whole leg decorative.
rem The script derives the repository root from its own location instead.
rem
rem AND FIRST, THE CLASSIFIER IS ASKED TO PROVE ITSELF.  tools\classify-asan.ps1
rem is the only thing standing between a use-after-free and a release, and until
rem now the suite that checks it ran only when somebody remembered to run it --
rem which is precisely the argument this whole file makes against a manual ASan
rem leg, applied to the component that decides whether the ASan leg can speak.
rem
rem The objection was that this would be a seventh step and the six-step output
rem shape is a contract.  The shape is kept: six banners, same summary table,
rem same exit codes, one extra line inside step 6.  A component that gates a
rem gate, ungated, is a defect, and a convention loses to a defect.
rem
rem A self-test failure does NOT fall through to the classification.  A
rem classifier that fails its own fixtures has no verdict worth reading, and
rem running it anyway would produce an authoritative-looking answer from a thing
rem we have just proved is broken.  The leg goes red and says why.
rem
rem Budget: the suite runs its table in-process for this reason and costs ~4s.
rem If it ever creeps past ~10s here, fix it there -- do not delete this call.
rem
rem KEEP THIS FILE PURE ASCII.  MEASURED, AND IT COST AN HOUR.  verify.bat has
rem LF line endings, and cmd.exe parses a batch file by BYTE OFFSET while
rem counting the text it has consumed in CHARACTERS.  With CRLF the two happen
rem to stay in step; with LF plus one multi-byte character anywhere in the file,
rem they drift, and the parser resumes mid-line -- SOMEWHERE ELSE ENTIRELY.
rem Adding one Chinese sentence to the message below made cmd execute `set "R'
rem and then try to run `C_REL_BUILD=0' as a program, at line 56, a hundred and
rem sixty lines above the edit.  A gate that mis-executes its own first step
rem while still printing banner [1/6] is about the worst failure this file could
rem have, so: no non-ASCII here.  The Chinese half of the message is printed by
rem tools\test-classify-asan.ps1, which is a .ps1 with a UTF-8 BOM and gets the
rem console encoding right.
:classify_asan
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive ^
  -ExecutionPolicy Bypass -File "%ROOT%tools\test-classify-asan.ps1" -Quiet
set "RC_SELFTEST=%ERRORLEVEL%"
if not "%RC_SELFTEST%"=="0" (
  echo   [FAIL] ASan classifier self-test FAILED; this leg's verdict is not
  echo          trustworthy, so the run was NOT classified. The failing case and
  echo          the reason it exists are printed above. Fix
  echo          tools\classify-asan.ps1, or the fixture that caught it, first.
  set "RC_ASAN_TEST=asan"
  goto :eof
)

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive ^
  -ExecutionPolicy Bypass -File "%ROOT%tools\classify-asan.ps1" -LogPath "%ASAN_LOG%"
set "RC_CLASSIFY=%ERRORLEVEL%"
if "%RC_CLASSIFY%"=="0" goto :eof
if "%RC_CLASSIFY%"=="2" (
  echo   [known] every report above is third-party: neither the use site nor the
  echo           free site is GeeyoouUI code. Not counted against the gate.
  echo           Full log: "%ASAN_LOG%"
  goto :eof
)
if "%RC_CLASSIFY%"=="1" (
  echo   [FAIL] at least one AddressSanitizer report is attributable to GeeyoouUI.
  set "RC_ASAN_TEST=asan"
  goto :eof
)
echo   [FAIL] the ASan classifier reached no verdict ^(exit %RC_CLASSIFY%^).
echo          Counted as OURS on purpose -- see the table above this label.
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
