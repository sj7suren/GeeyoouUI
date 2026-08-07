@echo off
rem Fixture gate with `call :lint_doors` DELETED.
rem
rem This is the whole failure this predicate exists for: the file still runs to
rem the end, still prints a summary, and the door-coverage lint is simply never
rem invoked.  Nothing else in the tree can see that, because the lint reddens
rem the gate by setting a variable from inside a subroutine nobody calls.
rem
rem The label is left in place on purpose.  A dead label is not the defect; an
rem uninvoked checker is.
call :classify_asan
goto :eof

:lint_doors
goto :eof

:classify_asan
goto :eof
