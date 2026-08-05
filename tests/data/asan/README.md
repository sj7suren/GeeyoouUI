# tests/data/asan -- fixtures for the ASan gate classifier

Input for `tools/test-classify-asan.ps1`, which exercises
`tools/classify-asan.ps1` -- the thing that decides whether an AddressSanitizer
report in the `[6/6]` leg of `verify.bat` is a defect of ours or environmental
noise.

**Every log in this directory is hand-written**, in the format MSVC's ASan
runtime emits, and none of them is a capture of a real run. They exist because
the two cases that matter most are the two hardest to produce on demand:

* a third-party use-after-free that has our frames on its stack -- the
  SogouPY.ime case that this classifier was written for. It only fires when a
  particular IME happens to be installed on the machine running the gate.
* a real use-after-free of ours -- which we obviously do not keep one of
  around in the tree just to have a sample.

The paths in the fixtures are rooted at `E:\Develop\tools\GeeyoouUI`, which is
one developer's checkout. That is not a portability problem: the self-test
passes that same root to the classifier with `-RepoRoot`, so the fixtures and
the expectation agree with each other on any machine.

Expected verdicts live in `tools/test-classify-asan.ps1`, one table, next to the
reason each case exists. Add the case there before you change a rule.
