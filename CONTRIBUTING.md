# Contributing to GeeyoouUI

Thanks for looking. This file is short on ceremony and specific about what
actually helps.

**中文没问题** — issues, discussions and PR descriptions in Simplified Chinese
are welcome and will be answered in kind. Code comments should be English so the
codebase stays readable to everyone.

---

## Before anything else: what this project is

GeeyoouUI is an **MIT-licensed C++20 widget toolkit for Windows industrial HMI
and operator consoles**. It is not a general-purpose cross-platform GUI
framework today, and the README says so on purpose.

Two consequences worth knowing before you invest time:

- **Windows only.** The platform layer is 21 pure virtuals; X11 and Cocoa are
  scoped but unimplemented. If you want them, see "The highest-value
  contribution" below — it is a real, bounded, welcoming task.
- **Absolute coordinates are a supported style, not a backlog item.** On a mimic
  screen a pump drawn 40 px left of a valve is *process semantics*. A PR that
  "fixes" a page by reflowing it has destroyed information. Both layout and
  fixed geometry are first-class here.

Read [`docs/architecture.md`](docs/architecture.md) before your first change. It
records which trade-offs were made deliberately, which saves you the trouble of
"fixing" one of them.

## Build and gate

You need **Visual Studio with the C++ workload**. CMake, Ninja and MSVC all come
from VS itself — nothing needs to be on your `PATH`:

```
build.bat            :: Release  -> build\bin\showcase.exe
build-debug.bat      :: Debug    -> asserts live here
build-asan.bat       :: ASan     -> where the lifetime bugs surface
```

If VS lives somewhere unusual, edit `VSROOT` at the top of each script.

**Every PR must leave this green:**

```
verify.bat
```

That runs six steps: the door-coverage lint (and the lint's own self-test), then
Release / Debug / ASan — each built and each test suite run. It prints a summary
and exits non-zero if any leg failed. CI runs the same six steps on every push.

There is no "I'll fix the tests later" path here. A red gate on `master` in a
toolkit that people static-link into plant equipment is not a chore, it is a
liability.

## What actually helps

Ranked by how much it moves the project:

### 1. The highest-value contribution: an X11 or Cocoa backend

This is the single thing standing between GeeyoouUI and most of its potential
users, and it is **bounded work with a reference implementation to copy**:

- The whole porting surface is `include/geeyoou/platform/Platform.hpp` — 21 pure
  virtual methods, plus `HitZone` and `WindowOptions`.
- `src/platform/win32/Win32Platform.cpp` is the worked example.
- Everything above the platform layer is already portable: rendering goes
  through Blend2D (a CPU rasteriser), so a new backend only has to hand over a
  pixel buffer, an event stream and a window.
- Acceptance is mechanical: the widget tests pass and the showcase runs.

Open an issue before starting so we can talk through the event-loop and DPI
shape — not to gatekeep, but because those two are where the Win32 backend
learned things the header does not say.

### 2. Domain icon packs

Pumps, valves, breakers, conveyors, tanks. `icons().addSvgPath()` takes the `d`
attribute straight out of Lucide / Feather / Tabler / Material — they are all
24×24 grids with 2-unit strokes, which is this library's drawing grid, so most
icons drop in unchanged. See `examples/showcase/PlantIcons.cpp`.

### 3. Skins

A skin is a `Theme` plus a stylesheet — one file. Industrial palettes
(high-contrast control room, daylight-readable, colour-vision-safe alarm sets)
are genuinely useful and cost you almost nothing to publish.

### 4. Bug reports from real plant floors

The most valuable reports this project can get. A widget that misbehaves after
running for three weeks on a panel PC is worth more than ten synthetic repros.
Include the skin, the DPI, the Windows build, and what the screen was doing.

### 5. Documentation and translations

The README exists in English and Simplified Chinese. Both are maintained;
neither is a machine translation of the other. Other languages are welcome —
the showcase's own UI strings live in `examples/showcase/i18n/`, one file per
language, and adding one is a new file plus one line in `I18n.cpp`.

## Code style

Match what is already there; when in doubt, read the file you are editing.

- **2-space indent, ~80 columns**, `#pragma once`, namespace `geeyoou`.
- `PascalCase` types, `camelCase` functions, `trailing_` underscore on private
  members.
- **Comments explain WHY, not what.** This codebase's comments are load-bearing
  documentation — `core/Signal.hpp` explains the D7 contract, `Layout.hpp`
  explains why a Layout may never hold a `Widget*`. A comment that restates the
  code is noise; a comment that records the failure a line prevents is the most
  valuable thing in the file. Write those.
- **No new dependencies** without discussing it first. The entire stack is
  Blend2D + AsmJit, both Zlib-licensed, and keeping it permissive end-to-end is
  a feature, not an accident.
- **Nothing in a hot path may allocate.** Live data goes through fixed-capacity
  ring buffers because this software runs unattended for months.

## Lifetime rules (the ones that bite)

Most defects this project has shipped were lifetime bugs, so these are written
down rather than assumed:

- **A slot may not destroy the object that owns the signal it is running
  inside.** That is contract D7, documented in `core/Signal.hpp`.
- **Subscriptions that outlive their subscriber are use-after-frees.** Use
  `ConnectionScope`, declared *last* in the class so it is destroyed *first*.
- **Anything holding a pointer INTO a widget subtree must let go before that
  subtree is destroyed.** See `Shell::pagesAboutToRebuild` for the pattern.
- Run `build-asan.bat` before you send a PR that touches ownership. ASan finds
  in seconds what a control room finds in weeks.

## Commits and PRs

- **One commit per file** is this repository's convention — it keeps `revert`
  and `cherry-pick` surgical.
- Commit messages follow `area: statement`, where the statement says what is
  *true now* or what was *found*, not what was typed:

  ```
  scene3d: the viewport gets its own ground, and shading only ever darkens
  test: a white model must be visible on a white skin, as a number
  showcase: drop the alarm sink before the page holding its widget dies
  ```

- PRs should say **what failure the change prevents**. "Adds null check" tells a
  reviewer nothing; "a page rebuilt while the header menu was open dereferenced
  the freed popup" tells them everything.
- Small and focused beats large and comprehensive. A 200-line PR gets reviewed;
  a 2000-line one gets postponed.

## Reporting a bug

Open an issue with:

- What you saw and what you expected
- Windows version, DPI scaling, and which skin was active
- A minimal repro if you can — a `main()` that builds against the library is
  ideal, but a description of the page and the sequence of clicks is fine
- ASan output if you have it (`build-asan.bat`, then run the binary)

## Security

Found something with security impact? Please **do not** open a public issue —
see [`SECURITY.md`](SECURITY.md).

---

By contributing you agree that your work is licensed under the
[MIT License](LICENSE), the same terms as the rest of the project.
