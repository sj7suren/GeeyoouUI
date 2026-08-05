//
// The negative half of contract D7, and the only case in the suite that expects
// the process to die.
//
// D7 (core/Signal.hpp): a slot may destroy anything EXCEPT the object that owns
// the signal it is running inside.  Two mechanisms enforce it, and they are not
// the same strength:
//
//   * an assert() in ~Signal, which exists only where NDEBUG is not defined.
//     Until build-debug.bat landed this project had no such build, so the
//     contract had never once been executed.  A rule nothing runs is a comment.
//   * a DEGRADED guarantee that is compiled into every build, Release included:
//     ~Signal clears the control block's `alive` flag and emit() re-reads it
//     once per slot, so the emission stops at the violation instead of walking
//     a freed slot list.
//
// The Debug half cannot be caught in-process: the whole point is that the stack
// below the assert is already invalid.  So the case re-runs THIS EXECUTABLE as
// a child with GEEYOOU_D7_SUICIDE=1, the child breaks the contract on purpose,
// and the parent reads the outcome back:
//
//   Debug   -- the child must die on the assert, and say why on stderr.
//   Release -- the assert is gone, so the child must survive the violation and
//              exit with the code that means "the emission stopped where it was
//              supposed to".  That is not a pass for the contract -- violating
//              it is still a bug in the caller -- but it IS the assertion that
//              Release fails predictably rather than corrupting the heap, which
//              is what an unattended upper computer needs from it.
//
// The in-process companion, which states the same outcome as an ordinary
// assertion in the Release suite, is
// signal.destroying_the_owner_from_a_slot_stops_the_emission.
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

// Exit codes the child uses, both far from anything runAll() can return (a
// capped failure count, or 125).
//
// kDegraded is the CONTRACTED Release outcome: the assert was compiled out, the
// violation happened, and the emission stopped at it.  kRanOn is the old
// use-after-free behaviour -- emit() kept dispatching out of a slot list that
// ~Signal had already destroyed -- and is a hard failure, not a warning.
constexpr int kDegraded = 42;
constexpr int kRanOn = 43;

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

// Three slots with the killer FIRST: the two behind it are what tells "the
// emission stopped" apart from "the emission carried on through freed memory",
// which from the outside would otherwise both look like a clean exit.
int violateD7() {
  Owner* owner = new Owner();
  int behind = 0;
  // The slot destroys the object that owns the signal it is running inside --
  // which unwinds ~Signal into a list emit() is still walking.
  owner->sig.connect([owner] { delete owner; });
  owner->sig.connect([&behind] { ++behind; });
  owner->sig.connect([&behind] { ++behind; });
  owner->sig.emit();
  return behind == 0 ? kDegraded : kRanOn;
}

}  // namespace

GEEYOOU_TEST(d7,
             destroying_the_signal_owner_is_caught_in_debug_contained_in_release) {
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
    const int outcome = violateD7();
    // Only reachable where the assert was compiled out -- and, since the
    // degraded guarantee landed, reachable in one piece rather than by luck.
    std::fflush(nullptr);
    std::exit(outcome);
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
  // Release: the assert is gone, so what is under test is the degraded
  // guarantee.  The child must have SURVIVED the violation (a crash, or the
  // WER exit codes a heap corruption produces, is neither of the two codes
  // below) and must have stopped the emission (kRanOn, not kDegraded, is what
  // deleting the `alive` check would produce here).
  if (rc == kRanOn) {
    GEEYOOU_FAIL(
        "D7 违约后 emit 继续分发了后续槽——降级保护失效，这正是 UAF 窗口");
  } else {
    CHECK_EQ(rc, kDegraded);
  }
  geeyoou::test::note(
      "[note] d7：Release 构建里 assert 被编译掉，D7 违约不会被“拦截”，"
      "但会被降级为确定性行为——发射就地停止，违约槽之后的槽不再触发"
      "（子进程退出码 42 即此）。要在违约现场当场中止，仍须用 "
      "build-debug.bat 跑 Debug。");
#else
  // Debug: the child must have died, and died ON THE CONTRACT rather than on
  // some unrelated crash further down.
  CHECK_NE(rc, 0);
  CHECK_NE(rc, kDegraded);
  CHECK_NE(rc, kRanOn);
  const bool named = text.find("Signal destroyed from inside its own emit") !=
                     std::string::npos;
  if (!named) {
    GEEYOOU_FAIL("子进程未在 D7 断言处停下，输出尾部：" +
                 text.substr(text.size() > 400 ? text.size() - 400 : 0));
  }
#endif
}
