@echo off
rem The subject of the CRLF half of Test-BatchFileHygiene.
rem
rem bat-eol-crlf\hygiene.bat and bat-eol-lf\hygiene.bat hold THE SAME
rem CHARACTERS.  The ONLY difference between the two files is the line
rem terminator, and the two cases that read them expect OPPOSITE verdicts.
rem So the check is demonstrably about the bytes, and not about anything
rem else that happens to differ between two fixture directories.
rem
rem Pure ASCII in both, deliberately.  bat-nonascii\armed.bat covers the
rem other half of the conjunction; a fixture that tripped BOTH checks could
rem not tell you which of them fired, and a fixture you cannot read a
rem verdict from is not evidence.
echo hygiene
