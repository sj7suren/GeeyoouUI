@echo off
rem AddressSanitizer build of GeeyoouUI, in its own tree next to the other two.
rem
rem THIS IS THE THIRD LEG OF THE GATE, and it exists for the same reason the
rem second one does.  Release and Debug disagree about NDEBUG and about
rem _ITERATOR_DEBUG_LEVEL, so each checks something the other structurally
rem cannot -- and NEITHER of them checks memory safety at all.  Every one of the
rem five use-after-frees the R2 reviews found was GREEN in both configurations
rem and visible only under /fsanitize=address: a freed Widget's vtable pointer
rem is still the right bits for a few microseconds, so the pass finishes, the
rem numbers come out right and the suite says PASS.
rem
rem AND YOUR TEST MUST LET THE READ SURVIVE TO THE SANITISER.  This leg only
rem sees loads that are still in the binary: `(void)w->geometry();` is a load
rem with no consumer, /O2 deletes it outright, and a deleted load is a
rem use-after-free ASan never reports.  Measured, not feared -- E14's red state
rem produced FAILING cases and ZERO reports until the value was stored into an
rem observable member (tests\widget\test_removal.cpp, lastCachedWidth).  So:
rem CONSUME every read of a possibly-freed object, with a CHECK.  "The case went
rem red" and "ASan went red" are two INDEPENDENT signals, and a case that only
rem produces the first one is not evidence about this leg at all.
rem
rem RelWithDebInfo, not Debug: ASan wants optimised code (its shadow-memory
rem checks are cheap, the interceptors are not) and NDEBUG means the asserts do
rem not pre-empt the reports.  /Zi is already in the RelWithDebInfo flags; it is
rem repeated here because a report without symbols is a report nobody can act
rem on.
setlocal

set "ROOT=%~dp0"
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

rem Reuse the blend2d/asmjit checkouts the Release tree already fetched when
rem they are there.  Not a hard dependency -- a clean machine that runs this
rem first simply fetches its own -- but on a developer box it turns a two-minute
rem clone into nothing.
set "REUSE="
if exist "%ROOT%build\_deps\blend2d-src" if exist "%ROOT%build\_deps\asmjit-src" (
  set "REUSE=-DFETCHCONTENT_SOURCE_DIR_ASMJIT=%ROOT%build/_deps/asmjit-src -DFETCHCONTENT_SOURCE_DIR_BLEND2D=%ROOT%build/_deps/blend2d-src"
)

rem Examples off: showcase.exe -- a GUI that wants a message loop and a human --
rem is not what this leg is for.
rem
rem THIS SWITCH IS WHY tests/CMakeLists.txt HAS TO LIST PAGE SOURCES EXPLICITLY.
rem With examples off, examples/showcase/CMakeLists.txt is never even added, so
rem a page that appears ONLY there is not compiled by this leg at all -- not
rem built, not warned about, not instrumented.  That is exactly how PageIcons
rem and PageLayout reached review having never met /W4 /permissive- or ASan,
rem while a comment here claimed the pages were "compiled into the test binary
rem anyway".  It was true of the three pages that existed when it was written
rem and false the day a fourth landed.
rem
rem So: coverage of showcase code on this leg is whatever tests/CMakeLists.txt
rem names, and nothing else.  Do not read this switch as "the examples are
rem covered elsewhere".
rem
rem /DWIN32 /D_WINDOWS /EHsc are NOT decoration: setting CMAKE_CXX_FLAGS on the
rem command line REPLACES the flags CMake would have computed, it does not add
rem to them, and those three are exactly what the Release and Debug trees get
rem for free (check CMAKE_CXX_FLAGS:STRING in either CMakeCache.txt).  Dropping
rem them meant this leg compiled a configuration NOTHING ELSE compiles -- no C++
rem unwind semantics -- so the one leg whose entire job is memory safety was not
rem building the objects that ship.  It said so on every clean build, warning
rem C4530 out of <chrono>; incremental builds never recompiled the affected
rem translation units, so nobody had seen it in a long time.  Add sanitiser
rem flags here, never remove these.
"%CMAKE%" -G Ninja -S "%ROOT%." -B "%ROOT%build-asan" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /fsanitize=address /Zi" ^
  -DGEEYOOU_BUILD_EXAMPLES=OFF ^
  %REUSE%
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%ROOT%build-asan"
if errorlevel 1 exit /b 1

rem The ASan runtime is a DLL and it is not on PATH.  Copied rather than added
rem to PATH so the binary is runnable from anywhere, including from a CI step
rem that does not inherit this script's environment.
for /f "delims=" %%D in ('dir /b /s "%VSROOT%\VC\Tools\MSVC\*\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" 2^>nul') do (
  copy /y "%%D" "%ROOT%build-asan\bin\" >nul
)
if not exist "%ROOT%build-asan\bin\clang_rt.asan_dynamic-x86_64.dll" (
  echo [error] ASan runtime DLL not found under "%VSROOT%\VC\Tools\MSVC"
  exit /b 1
)

echo.
echo [ok] ASan tests at %ROOT%build-asan\bin\geeyoou_tests.exe
