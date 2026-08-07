@echo off
rem Fixture gate where the call survives ONLY as a comment.
rem
rem This is why the predicate strips batch comments before it looks.  The real
rem verify.bat carries more `rem` than code and those comments quote the very
rem lines they document, so a raw text match is satisfied by a sentence ABOUT
rem the call that somebody just deleted -- the most specific way this check
rem could be useless.
rem
rem The line below is a comment.  It is not a call.
rem call :lint_doors
call :classify_asan
goto :eof

:lint_doors
goto :eof

:classify_asan
goto :eof
