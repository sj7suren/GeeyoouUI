#include "framework/Test.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>  // _aligned_malloc / _aligned_free (MSVC)
#if defined(_MSC_VER)
#include <crtdbg.h>  // report modes, so a Debug run never opens a dialog
#endif
#include <new>
#include <vector>

// --- global allocation counter ----------------------------------------------
//
// Replacing the global operator new/delete is the only way to see allocations
// that happen INSIDE the library under test -- the std::function a Signal
// stores, the vector a Widget grows.  A per-object counting allocator would see
// none of them.
//
// The counter is a namespace-scope std::atomic, which is constant-initialised
// (std::atomic's constructor is constexpr), so it is live before any dynamic
// initialisation and therefore before the first possible operator new call.  A
// function-local static would introduce exactly the chicken-and-egg this avoids.
namespace {
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<std::uint64_t> g_freeCount{0};

void* gyAllocate(std::size_t bytes) {
  g_allocCount.fetch_add(1, std::memory_order_relaxed);
  // malloc(0) may legally return nullptr, which operator new must not.
  void* p = std::malloc(bytes != 0 ? bytes : 1);
  if (!p) throw std::bad_alloc();
  return p;
}

void* gyAllocateAligned(std::size_t bytes, std::size_t align) {
  g_allocCount.fetch_add(1, std::memory_order_relaxed);
  void* p = _aligned_malloc(bytes != 0 ? bytes : 1, align);
  if (!p) throw std::bad_alloc();
  return p;
}

// delete(nullptr) is legal and extremely common (every unique_ptr that was
// moved from ends in one), so it must not register as a free -- otherwise the
// zero-free gates below would be measuring how many empty pointers went out of
// scope.
void gyFree(void* p) {
  if (!p) return;
  g_freeCount.fetch_add(1, std::memory_order_relaxed);
  std::free(p);
}

void gyFreeAligned(void* p) {
  if (!p) return;
  g_freeCount.fetch_add(1, std::memory_order_relaxed);
  _aligned_free(p);
}
}  // namespace

// The whole family has to be replaced together: mixing our malloc'd blocks with
// the CRT's free() is undefined behaviour, and MSVC emits sized and aligned
// deletes on its own.
void* operator new(std::size_t n) { return gyAllocate(n); }
void* operator new[](std::size_t n) { return gyAllocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  try { return gyAllocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  try { return gyAllocate(n); } catch (...) { return nullptr; }
}
void* operator new(std::size_t n, std::align_val_t a) {
  return gyAllocateAligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
  return gyAllocateAligned(n, static_cast<std::size_t>(a));
}

void operator delete(void* p) noexcept { gyFree(p); }
void operator delete[](void* p) noexcept { gyFree(p); }
void operator delete(void* p, std::size_t) noexcept { gyFree(p); }
void operator delete[](void* p, std::size_t) noexcept { gyFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { gyFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { gyFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { gyFreeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { gyFreeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { gyFreeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { gyFreeAligned(p); }

namespace geeyoou::test {
namespace {

// Intrusive list head.  Constant-initialised for the same reason the counter is.
Registrar* g_head = nullptr;

std::vector<std::string>& notes() {
  static std::vector<std::string> v;
  return v;
}

const char* g_invalidReason = nullptr;

}  // namespace

std::uint64_t allocCount() {
  return g_allocCount.load(std::memory_order_relaxed);
}

std::uint64_t freeCount() {
  return g_freeCount.load(std::memory_order_relaxed);
}

Registrar::Registrar(const char* s, const char* n, TestFn f)
    : suite(s), name(n), fn(f), next(g_head) {
  g_head = this;
}

void Context::fail(const char* file, int line, const std::string& message) {
  ++failures_;
  std::printf("    %s(%d): %s\n", file, line, message.c_str());
}

void invalidateRun(const char* why) { g_invalidReason = why; }

void note(std::string line) { notes().push_back(std::move(line)); }

std::string formatDouble(double v) {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string formatPointer(const void* p) {
  if (!p) return "nullptr";
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%p", p);
  return buf;
}

int runAll() {
  // Static-init order across translation units is unspecified, so the intrusive
  // list arrives in an arbitrary order.  Sorting makes the output diffable
  // between runs and between machines.
  std::vector<const Registrar*> cases;
  for (const Registrar* r = g_head; r; r = r->next) cases.push_back(r);
  std::sort(cases.begin(), cases.end(), [](const Registrar* a, const Registrar* b) {
    const int bySuite = std::strcmp(a->suite, b->suite);
    return bySuite != 0 ? bySuite < 0 : std::strcmp(a->name, b->name) < 0;
  });

  int failedCases = 0;
  for (const Registrar* r : cases) {
    Context ctx;
    r->fn(ctx);
    const bool ok = ctx.failures() == 0;
    if (!ok) ++failedCases;
    std::printf("%s.%s  %s%s\n", r->suite, r->name, ok ? "PASS" : "FAIL",
                ctx.aborted() ? " (已中止)" : "");
  }

  std::printf("\n%zu 个用例，%d 个失败\n", cases.size(), failedCases);
  for (const std::string& n : notes()) std::printf("%s\n", n.c_str());

  if (g_invalidReason) {
    std::printf("\n!! %s\n", g_invalidReason);
    return 125;  // distinct from any plausible failure count
  }
  // Exit code IS the failure count, capped so it never collides with a shell's
  // reserved range or wraps to 0 on a POSIX host.
  return std::min(failedCases, 100);
}

}  // namespace geeyoou::test

int main() {
  // Unbuffered, so the case names printed so far survive a run that ENDS
  // BADLY.  A suite whose job includes proving that injected defects are caught
  // spends much of its life crashing on purpose, and a buffered stdout throws
  // away exactly the line that says where.  86 lines of printf; the cost does
  // not register.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

#if defined(_MSC_VER) && defined(_DEBUG)
  // A Debug run has to be able to complete unattended.  Every assert the CRT,
  // the STL's checked iterators or the library itself raises would otherwise
  // open a MODAL DIALOG, and an automated run would hang there rather than
  // fail -- which is worse than either passing or failing.  Sent to stderr
  // instead, an assertion ends the process with a diagnosable exit code.
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
  return geeyoou::test::runAll();
}
