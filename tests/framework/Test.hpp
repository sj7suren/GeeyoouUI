#pragma once
//
// A unit-test harness in two files, with no third-party dependency.
//
// GeeyoouUI ships as a static library that customers drop into their own build;
// dragging gtest/Catch2 in would put a second test framework -- and a second
// opinion about main() -- into every one of those builds.  What a widget
// library actually needs from a harness is small: static registration, two
// assertion levels, and an allocation counter (docs/architecture.md section 1,
// rule 2 makes "how many times did that allocate" a first-class question).
//
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace geeyoou::test {

// Per-case state.  Handed to the body by the GEEYOOU_TEST macro as `ctx_`; the
// assertion macros below pick it up from that name.
class Context {
 public:
  void fail(const char* file, int line, const std::string& message);
  void abort() { aborted_ = true; }

  int failures() const { return failures_; }
  bool aborted() const { return aborted_; }

 private:
  int failures_ = 0;
  bool aborted_ = false;
};

using TestFn = void (*)(Context&);

// Static registration through an intrusive list: the node lives inside the
// Registrar object itself, so building the registry allocates nothing.  That
// matters because AllocGuard counts process-wide allocations, and a registry
// that allocated during static init would still be honest -- but a registry
// that allocated LAZILY, on first use, would land inside somebody's guard.
struct Registrar {
  Registrar(const char* suite, const char* name, TestFn fn);

  const char* suite;
  const char* name;
  TestFn fn;
  Registrar* next;
};

// Runs every registered case and returns the number that failed, which is also
// what main() uses as its exit code.
int runAll();

// Declares the whole run non-authoritative: runAll() prints `why` at the end
// and returns a non-zero code even with zero failures.  Used by the baseline
// rewrite mode -- a green exit code that "does not constitute a pass" is the
// exact green-light theatre this harness exists to prevent.
void invalidateRun(const char* why);

// Printed verbatim after the summary.  For warnings that must be seen but must
// not fail the run (text baselines, whose rasterisation is machine-dependent).
void note(std::string line);

// --- value formatting for the CHECK_EQ family --------------------------------
std::string formatDouble(double v);
std::string formatPointer(const void* p);

template <class T>
std::string toText(const T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    return v ? "true" : "false";
  } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
    // Before the pointer branch on purpose: `const char*` is both, and the
    // useful rendering is the text, not the address.
    return std::string(std::string_view(v));
  } else if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else if constexpr (std::is_integral_v<T>) {
    return std::to_string(v);
  } else if constexpr (std::is_floating_point_v<T>) {
    return formatDouble(static_cast<double>(v));
  } else if constexpr (std::is_pointer_v<T>) {
    return formatPointer(static_cast<const void*>(v));
  } else {
    return "<?>";
  }
}

// --- allocation counting -----------------------------------------------------
// Backed by a replacement of the global operator new/delete in Test.cpp, so it
// sees EVERY allocation in the process, including the ones std::function and
// std::vector make inside the library under test.
std::uint64_t allocCount();

class AllocGuard {
 public:
  AllocGuard() : start_(allocCount()) {}

  std::uint64_t count() const { return allocCount() - start_; }
  void reset() { start_ = allocCount(); }

 private:
  std::uint64_t start_;
};

}  // namespace geeyoou::test

// --- macros ------------------------------------------------------------------
//
// Defines and registers a case.  The body sees `ctx_`; [[maybe_unused]] keeps a
// case that only exercises a code path (and asserts nothing) warning-free
// under /W4.
#define GEEYOOU_TEST(suite, name)                                            \
  static void gy_case_##suite##_##name(::geeyoou::test::Context&);           \
  static const ::geeyoou::test::Registrar gy_reg_##suite##_##name(           \
      #suite, #name, &gy_case_##suite##_##name);                             \
  static void gy_case_##suite##_##name(                                      \
      [[maybe_unused]] ::geeyoou::test::Context& ctx_)

#define GEEYOOU_FAIL(msg) ctx_.fail(__FILE__, __LINE__, (msg))

#define CHECK(expr)                                                          \
  do {                                                                       \
    if (!(expr)) GEEYOOU_FAIL("CHECK(" #expr ") 为假");                       \
  } while (0)

// Aborts the CASE, not the run.  Note it returns from the enclosing function,
// so REQUIRE inside a lambda only leaves the lambda -- use CHECK there.
#define REQUIRE(expr)                                                        \
  do {                                                                       \
    if (!(expr)) {                                                           \
      GEEYOOU_FAIL("REQUIRE(" #expr ") 为假");                                \
      ctx_.abort();                                                          \
      return;                                                                \
    }                                                                        \
  } while (0)

#define GEEYOOU_CMP(a, b, op, label)                                         \
  do {                                                                       \
    const auto& gy_a_ = (a);                                                 \
    const auto& gy_b_ = (b);                                                 \
    if (!(gy_a_ op gy_b_)) {                                                 \
      GEEYOOU_FAIL(std::string(label "(" #a ", " #b ") 为假：") +             \
                   ::geeyoou::test::toText(gy_a_) + " vs " +                 \
                   ::geeyoou::test::toText(gy_b_));                          \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b) GEEYOOU_CMP(a, b, ==, "CHECK_EQ")
#define CHECK_NE(a, b) GEEYOOU_CMP(a, b, !=, "CHECK_NE")
#define CHECK_LT(a, b) GEEYOOU_CMP(a, b, <, "CHECK_LT")
#define CHECK_GE(a, b) GEEYOOU_CMP(a, b, >=, "CHECK_GE")

// Float comparison with an explicit tolerance -- there is no default, because a
// default epsilon is how a geometry test silently stops testing anything.
#define CHECK_NEAR(a, b, eps)                                                \
  do {                                                                       \
    const double gy_a_ = static_cast<double>(a);                             \
    const double gy_b_ = static_cast<double>(b);                             \
    const double gy_d_ = gy_a_ > gy_b_ ? gy_a_ - gy_b_ : gy_b_ - gy_a_;      \
    if (!(gy_d_ <= static_cast<double>(eps))) {                              \
      GEEYOOU_FAIL(std::string("CHECK_NEAR(" #a ", " #b ") 超差：") +          \
                   ::geeyoou::test::formatDouble(gy_a_) + " vs " +           \
                   ::geeyoou::test::formatDouble(gy_b_));                    \
    }                                                                        \
  } while (0)
