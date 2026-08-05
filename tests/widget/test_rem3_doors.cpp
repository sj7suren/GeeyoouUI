//
// What the five REM3 checkpoints actually RETURN, and how often giving up is
// counted.
//
// The reproducers for the five doors live in the soak (test_layout_soak.cpp):
// they run every cycle, and what they prove is that the process survives and
// that nothing accumulates.  That is the expensive half of the question and it
// is deliberately phrased as a negative -- nothing crashed, nothing grew.
//
// The two cases here are the positive half, and neither of them fits in a soak:
//
//   * CP-G1's degraded ANSWER is written down to the last float in section 11.3
//     of docs/iterations/02-layout-engine.md, and until now nothing had ever
//     read it.  A degraded frame returns a value that looks like a value; the
//     only thing that distinguishes "gave up correctly" from "computed
//     nonsense out of a freed object" is what the value IS.
//   * framesDegraded's unit is a FRAME (REM3-G8), and that is not the same as
//     an operation and not the same as a door.  One setContentSize can honestly
//     raise it twice.  That was true from the day E3/E4 landed and was written
//     down nowhere, so the obvious assertion -- "one degradation per door" --
//     was waiting to be written and to be wrong.
//
#include <cstddef>
#include <cstdint>
#include <functional>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GroupBox;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {

// The frame GroupBox cuts out of itself, restated here because the constants
// are private to GroupBox.cpp.  RESTATED ON PURPOSE rather than exported: these
// four numbers are section 11.3's contract for what a dead GroupBox answers,
// and a test that imported them from the implementation would agree with
// whatever the implementation did.
constexpr float kInsetX = 12.0f;
constexpr float kInsetBottom = 12.0f;
constexpr float kTopPlain = 12.0f;
constexpr float kTopTitled = 34.0f;

class Sized : public Widget {
 public:
  explicit Sized(float w = 30.0f, float h = 20.0f) : w_(w), h_(h) {}

  SizeHint sizeHint() const override {
    return SizeHint{Size{w_, h_}, Size{w_, h_},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

 private:
  float w_;
  float h_;
};

// Fires once from inside sizeHint() -- the measuring door.  Same shape as the
// soak's, including the reason the answer is built BEFORE the callback: the
// callback destroys an ancestor of this widget, so `this` is gone by the time
// it returns.
class HintHook : public Sized {
 public:
  HintHook() : Sized(30.0f, 20.0f) {}
  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    const SizeHint out = Sized::sizeHint();
    if (once) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

// ...and one that waits for the RIGHT door.
//
// A ScrollArea::setContentSize reaches this widget's sizeHint() twice: once
// from inside the arrange that CP-C1's door starts, and once from the pure
// measurement relayout() issues afterwards.  Firing on a call COUNT would pin
// the case to today's call sequence, which is exactly the sort of assertion
// that turns into scenery when somebody adds a round.  The phase is what the
// case actually means, and the engine already publishes it: inside the arrange
// a layout pass is on the stack, and inside relayout's measurement none is.
class SecondDoorHook : public Sized {
 public:
  SecondDoorHook() : Sized(30.0f, 20.0f) {}
  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    const SizeHint out = Sized::sizeHint();
    if (once && !geeyoou::detail::layoutPassActive()) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

std::uint32_t degraded() {
  return geeyoou::detail::layoutDiagnostics().framesDegraded;
}

}  // namespace

// =========================================================== CP-G1 ==========
//
// Section 11.3, written out: `h.min == h.preferred == {frameW, frameH}`, `h.max`
// left at its default, `inner` and `titleW` counted as zero BY DEFINITION and
// never computed.
//
// The two halves of the case are the titled and the untitled box, and they are
// both here for one reason: they are what pins `frameH` to the value `top` had
// IN FRONT OF THE DOOR.  A degraded answer that had recomputed `top` after the
// door would have to read title_ out of a freed std::string, and the two halves
// would stop differing by the 22 pixels of title rule that separate them.
GEEYOOU_TEST(rem3_doors, a_dead_group_box_answers_with_its_frame_and_nothing_else) {
  geeyoou::detail::resetLayoutDiagnostics();

  // --- titled ---------------------------------------------------------------
  {
    Widget root;
    GroupBox* box = root.add<GroupBox>();
    box->setTitle("参数");
    BoxLayout* inner = box->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = box->add<HintHook>();
    inner->addWidget(trigger, 1);
    inner->addWidget(box->add<Sized>(60.0f, 40.0f), 1);

    // Armed LAST: every addWidget above marks the tree dirty and runs a pass of
    // its own, and a hook armed earlier would fire in one of those.
    trigger->once = [&root, box] { root.removeChild(box); };

    const std::uint32_t before = degraded();
    const SizeHint dead = box->sizeHint();

    // `box` is a dangling pointer from here on and is never named again.
    CHECK(root.children().empty());
    CHECK_EQ(degraded(), before + 1);

    // The answer, to the float.  24 = 2 * kInsetX, 46 = kTopTitled +
    // kInsetBottom: all that is left of a GroupBox that no longer exists is the
    // frame it was drawing.
    CHECK_EQ(dead.min.width, 2.0f * kInsetX);
    CHECK_EQ(dead.min.height, kTopTitled + kInsetBottom);
    // preferred == min: monotone, and it never claims room for a widget that is
    // not there.  Asserted against min rather than against the literals, so
    // this line keeps meaning "the two agree" if the literals ever move.
    CHECK_EQ(dead.preferred.width, dead.min.width);
    CHECK_EQ(dead.preferred.height, dead.min.height);
    // max untouched.  A degraded frame that had built a SizeHint field by field
    // would most likely have left this at zero, which reads as "refuses to be
    // any size at all" and would collapse whatever box holds it.
    CHECK_EQ(dead.max.width, geeyoou::kUnbounded);
    CHECK_EQ(dead.max.height, geeyoou::kUnbounded);
  }

  // --- untitled -------------------------------------------------------------
  {
    Widget root;
    GroupBox* box = root.add<GroupBox>();
    BoxLayout* inner = box->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = box->add<HintHook>();
    inner->addWidget(trigger, 1);
    inner->addWidget(box->add<Sized>(60.0f, 40.0f), 1);
    trigger->once = [&root, box] { root.removeChild(box); };

    const std::uint32_t before = degraded();
    const SizeHint dead = box->sizeHint();

    CHECK(root.children().empty());
    CHECK_EQ(degraded(), before + 1);
    CHECK_EQ(dead.min.width, 2.0f * kInsetX);
    CHECK_EQ(dead.min.height, kTopPlain + kInsetBottom);
    CHECK_EQ(dead.preferred.width, dead.min.width);
    CHECK_EQ(dead.preferred.height, dead.min.height);
    CHECK_EQ(dead.max.width, geeyoou::kUnbounded);
    CHECK_EQ(dead.max.height, geeyoou::kUnbounded);
  }

  geeyoou::detail::resetLayoutDiagnostics();
}

// ================================================ framesDegraded's unit ======
//
// ONE call to setContentSize, TWO degraded frames, and both of them are right.
//
// The soak's scrollContent group kills the scroll area at CP-C1's door, so
// setContentSize gives up at its first checkpoint and raises the counter once.
// This case kills it one door later, and then the arithmetic is different:
//
//   * relayout() is a frame of its own.  Its content_->sizeHint() is the door,
//     CP-S1 finds the tree gone, and IT gives up -- one frame, one record;
//   * relayout() returns to setContentSize, which is a DIFFERENT frame that has
//     been standing on the same three pointers all along.  Its CP-C2 asks the
//     same five questions, gets the same answers, and gives up too -- a second
//     frame, a second record.
//
// REM3-G8 says once per FRAME, so two is not a double count; it is the rule
// working.  What it rules out is the assertion that looks natural and is wrong:
// framesDegraded is NOT the number of doors that were crossed, NOT the number
// of operations that failed, and NOT the number of objects that died.  Section
// 11.3 now says so; this is what holds it there.
GEEYOOU_TEST(rem3_doors, one_operation_can_degrade_two_frames) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 800.0f, 600.0f});

  ScrollArea* area = root.add<ScrollArea>();
  // Sized BEFORE the content gets a layout, so relayout() takes its
  // hand-placed branch during set-up and no measurement happens outside a pass
  // until the one this case is about.
  area->setGeometry({0.0f, 0.0f, 200.0f, 150.0f});

  Widget* content = area->content();
  BoxLayout* box = content->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  SecondDoorHook* trigger = content->add<SecondDoorHook>();
  box->addWidget(trigger, 1);
  box->addWidget(content->add<Sized>(50.0f, 30.0f), 1);

  trigger->once = [&root, area] { root.removeChild(area); };

  const std::uint32_t before = degraded();
  area->setContentSize({320.0f, 260.0f});

  // `area`, `content` and `trigger` are all dangling from here on.
  CHECK(root.children().empty());
  CHECK_EQ(degraded(), before + 2);

  // ...and the give-up left nothing behind: both frames returned without
  // touching anything, the content's layout was parked while its measurement
  // was on the stack, and the park list drained on the way out.
  CHECK_EQ(geeyoou::detail::parkedLayoutCount(), std::size_t(0));
  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));

  geeyoou::detail::resetLayoutDiagnostics();
}
