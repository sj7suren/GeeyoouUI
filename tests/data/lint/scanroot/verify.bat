@echo off
rem Fixture gate for Test-GateStillCallsItsCheckers.
rem
rem Every fixture root needs one, because a root with no verify.bat is RED by
rem design: "the gate is not there" is not a green condition.  This is the
rem GREEN shape -- both calls present, both labels present.
call :lint_doors
call :classify_asan
goto :eof

:lint_doors
goto :eof

:classify_asan
goto :eof
