# tests/data/asan -- fixtures for the ASan gate classifier

Input for `tools/test-classify-asan.ps1`, which exercises
`tools/classify-asan.ps1` -- the thing that decides whether an AddressSanitizer
report in the `[6/6]` leg of `verify.bat` is a defect of ours or environmental
noise.

**This suite runs inside the gate.** `verify.bat`'s `:classify_asan` subroutine
calls the self-test before it classifies anything, and a failure here reddens
the leg without classifying at all. Adding a fixture costs no measurable wall
clock (the table runs in-process); the whole suite is about four seconds.

**Every log in this directory is hand-written**, in the format MSVC's ASan
runtime emits, and none of them is a capture of a real run. They exist because
the cases that matter most are the hardest to produce on demand:

* a third-party use-after-free that has our frames on its stack -- the
  SogouPY.ime case that this classifier was written for. It only fires when a
  particular IME happens to be installed on the machine running the gate.
* a real use-after-free of ours -- which we obviously do not keep one of
  around in the tree just to have a sample.
* a run that died half way through, which is what the gate will see the day the
  toolchain stops supporting `continue_on_error`.

## Two things every fixture must carry

**The suite summary line.** Every log except `no-suite-summary.log` ends the
test output with the line `tests/framework/Test.cpp` prints last, in the real
wording (`N 个用例，M 个失败`). The classifier refuses to reach a verdict
without it -- see the integrity sentinel in `classify-asan.ps1` -- so a fixture
that lacks it is testing `exit 3` whether it meant to or not.

**A deliberate mix of encodings.** The line is UTF-8 in every fixture except
`third-party-sogou-uaf-stock-thunk.log`, where it is GBK. The suite is compiled
`/utf-8` today, so UTF-8 is what a real log contains; GBK is what the same words
would become if that flag were ever dropped. The sentinel matches the *shape* of
the line rather than its bytes, and the odd fixture out is what keeps that claim
tested. Do not "normalise" it.

## The paths

The paths in the fixtures are rooted at `E:\Develop\tools\GeeyoouUI`, which is
one developer's checkout. That is not a portability problem: the self-test
passes that same root to the classifier with `-RepoRoot`, so the fixtures and
the expectation agree with each other on any machine.

## Where the expectations live

In `tools/test-classify-asan.ps1`, one table, next to the reason each case
exists. Some cases assert on the printed *text* as well as the exit code --
the ownership (`alloc:`) judgment is in shadow mode and has no exit code of its
own, so it is only tested if the text is.

**Add the case there before you change a rule.** No predicate in
`classify-asan.ps1` may be widened without a fixture carrying the real log text
first. The gate now enforces this by running the suite.
