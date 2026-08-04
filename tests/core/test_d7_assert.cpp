//
// The negative half of contract D7, and the only case in the suite that expects
// the process to die.
//
// D7 (core/Signal.hpp): a slot may destroy anything EXCEPT the object that owns
// the signal it is running inside.  The enforcement is an assert() in ~Signal,
// which means it exists ONLY in a build that defines no NDEBUG -- and until
// build-debug.bat landed, this project had no such build, so the contract had
// never once been executed.  A rule nothing runs is a comment.
//
// A violation cannot be caught in-process: the whole point is that the stack
// below the assert is already invalid.  So the case re-runs THIS EXECUTABLE as
// a child with GEEYOOU_D7_SUICIDE=1, the child breaks the contract on purpose,
// and the parent reads the outcome back:
//
//   Debug   -- the child must die on the assert, and say why on stderr.
//   Release -- the assert is compiled out, so the child must reach the end of
//              the violation and exit with a code of its own.  That is not a
//              pass for the contract; it is this suite stating in writing that
//              Release does not enforce it.
//
#include <cstdio>
#include <cstdlib>
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/core/Signal.hpp"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

using geeyoou::Signal;

namespace {

constexpr const char* kChildEnv = "GEEYOOU_D7_SUICIDE";

// Exit code the child uses for "I violated D7 and nothing stopped me".  Far
// from anything runAll() can return (a capped failure count, or 125).
constexpr int kNotCaught = 42;

bool envFlagOn(const char* name) {
#ifdef _MSC_VER
  std::size_t len = 0;
  char buf[16] = {};
  if (getenv_s(&len, buf, sizeof(buf), name) != 0) return false;
  return len > 1 && buf[0] != '0';
#else
  const char* v = std::getenv(name);
  return v && v[0] != '\0' && v[0] != '0';
#endif
}

// Path to this executable, without dragging <windows.h> into the test suite.
std::string selfPath() {
#ifdef _MSC_VER
  char* p = nullptr;
  if (_get_pgmptr(&p) != 0 || !p) return {};
  return p;
#else
  return {};
#endif
}

// The violation itself, kept in one place so the comment above it is the only
// place this pattern is ever written down.
struct Owner {
  Signal<> sig;
};

void violateD7() {
  Owner* owner = new Owner();
  // The slot destroys the object that owns the signal it is running inside --
  // which unwinds ~Signal into a list emit() is still walking.
  owner->sig.connect([owner] { delete owner; });
  owner->sig.emit();
}

}  // namespace

GEEYOOU_TEST(d7, destroying_the_signal_owner_from_a_slot_is_caught_in_debug) {
  if (envFlagOn(kChildEnv)) {
    // --- child ---
#if defined(_MSC_VER)
    // A failed assert can end up in a MODAL DIALOG, which in an automated run
    // means a hang rather than a failure.  Three knobs decide that, and which
    // one applies depends on the CRT flavour, so all three are set: the debug
    // report mode, the CRT error mode, and abort()'s hand-off to Windows Error
    // Reporting.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::fflush(stdout);
    violateD7();
    // Only reachable where the assert was compiled out.
    std::fflush(nullptr);
    std::exit(kNotCaught);
  }

  // --- parent ---
  const std::string exe = selfPath();
  REQUIRE(!exe.empty());

  // Child output goes to a file rather than to our own stdout: it re-runs the
  // whole suite on its way to the case above, and that noise would bury the
  // real summary.  The file is then the evidence.
  const std::string log = exe + ".d7.log";
  // The outer pair of quotes is for cmd.exe, which strips them and leaves the
  // inner ones -- the standard incantation for a quoted program AND a redirect.
  const std::string cmd =
      "\"\"" + exe + "\" > \"" + log + "\" 2>&1\"";

#ifdef _MSC_VER
  REQUIRE(_putenv_s(kChildEnv, "1") == 0);
#endif
  const int rc = std::system(cmd.c_str());
#ifdef _MSC_VER
  _putenv_s(kChildEnv, "");
#endif

  std::string text;
  std::FILE* f = nullptr;
#ifdef _MSC_VER
  if (fopen_s(&f, log.c_str(), "rb") != 0) f = nullptr;
#else
  f = std::fopen(log.c_str(), "rb");
#endif
  if (f) {
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) != 0) text.append(buf, n);
    std::fclose(f);
  }
  std::remove(log.c_str());

#ifdef NDEBUG
  // Release: the contract is documented but unenforced.  Asserting that out
  // loud is the point -- it is why build-debug.bat exists.
  CHECK_EQ(rc, kNotCaught);
  geeyoou::test::note(
      "[note] d7：Release 构建里 assert 被编译掉，D7 违约不会被拦截"
      "（用 build-debug.bat 跑 Debug 才会触发）");
#else
  // Debug: the child must have died, and died ON THE CONTRACT rather than on
  // some unrelated crash further down.
  CHECK_NE(rc, 0);
  CHECK_NE(rc, kNotCaught);
  const bool named = text.find("Signal destroyed from inside its own emit") !=
                     std::string::npos;
  if (!named) {
    GEEYOOU_FAIL("子进程未在 D7 断言处停下，输出尾部：" +
                 text.substr(text.size() > 400 ? text.size() - 400 : 0));
  }
#endif
}
