# Security Policy

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Use GitHub's private reporting instead:
**[Security → Report a vulnerability](https://github.com/sj7suren/GeeyoouUI/security/advisories/new)**

That channel is private between you and the maintainers until a fix is
published. If private reporting is not enabled on the repository yet, open a
regular issue that says only *"security report, please open a private channel"*
— with no details — and one will be opened for you.

Expect a first response within **7 days**.

## What is in scope

GeeyoouUI is a widget toolkit that gets **statically linked into other people's
applications**, some of which run on plant equipment. The parts most worth your
attention are the ones that parse or measure input the application did not
write:

| Area | Why it matters |
|---|---|
| `render/VectorPath.hpp` — SVG path parser | Takes a `d` attribute string, possibly from a file or an operator-editable config |
| `render/StyleSheet.hpp` — QSS-like parser | Stylesheets are content, and the showcase lets an operator type one at runtime |
| `render/Painter` text and glyph handling | Arbitrary UTF-8, including malformed sequences |
| `core/DataQueue`, `hmi/DataHub` | Cross-thread queues fed by acquisition code |
| `widget/TableModel` / `TreeTableModel` | Pull-model callbacks driven by row indices the view computes |

Memory-safety defects (out-of-bounds read/write, use-after-free, integer
overflow leading to either) in the above are in scope even without a
demonstrated exploit — in this problem domain, "it only crashes" is still an
outage on someone's line.

## What is out of scope

- Anything requiring the attacker to already run code in your process
- Denial of service by supplying absurd geometry (a 10⁹-row table is slow by
  arithmetic, not by defect)
- Vulnerabilities in Blend2D or AsmJit — please report those upstream; tell us
  too, so the pinned commit can be moved
- The `examples/showcase` demo application, which is documentation and is not
  intended to be deployed

## Design commitments that are security-relevant

These are properties the codebase deliberately maintains. A report showing one
of them is violated is a valid report:

- **Parsers never throw and never abort.** A malformed stylesheet drops the one
  bad rule; a malformed SVG path is rejected into `icons().errors()`. Content is
  not code, and bad content must not take down a control room screen.
- **An unregistered icon id draws nothing** rather than a placeholder. On a
  plant screen a stand-in that looks like a real symbol is more dangerous than
  blank space.
- **The acquisition thread never touches a `Widget`.** It may only `push()` into
  a bounded queue; overflow drops the oldest and counts it, and never grows
  unbounded.
- **Zero allocation on hot paths**, so long-running unattended operation cannot
  be pushed into memory exhaustion by data rate alone.

## Supported versions

The project is pre-1.0 and moves on `master`. Fixes land on `master`; there are
no maintained release branches yet. If you are shipping GeeyoouUI in a product,
pin a commit and watch this repository.
