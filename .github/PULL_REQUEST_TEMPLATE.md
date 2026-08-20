<!--
中文写没问题。

Keep this short. The two things a reviewer actually needs are: what failure
this prevents, and proof the gate is still green.
-->

## What failure does this prevent

<!--
Not "adds a null check" -- that tells a reviewer nothing. Say what breaks
without this change:

  "A page rebuilt while the header menu was open dereferenced the freed popup."
  "Under the light skin the chip kept a colour captured at build time."
  "20k rows made the pager recompute the whole page list on every scroll tick."
-->

## What changed

<!-- One or two sentences. The diff says the rest. -->

## Gate

<!-- verify.bat runs six steps: lint, Release, Debug, ASan -- each built and run. -->

- [ ] `verify.bat` is green locally
- [ ] Touched ownership or lifetimes? Then `build-asan.bat` was run and is clean
- [ ] New behaviour has a test — and the test fails without the fix
- [ ] Comments explain **why**, not what

## Screenshots

<!--
For anything visual, include BOTH a dark and a light skin. A colour captured at
build time looks perfect in the skin it was written under and unreadable in the
other one -- it is the most common visual regression this project has shipped.
-->

## Notes for the reviewer

<!--
Anything you are unsure about, deliberately left out, or want argued with.
Saying "I could not decide between A and B" is welcome, not a weakness.
-->
