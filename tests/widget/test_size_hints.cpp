//
// The size hints T-11 needed, on the nine controls that did not have one.
//
// A control without a sizeHint() answers with its NATURAL size -- the first
// geometry it was ever handed -- which for a widget built inside a layout is
// nothing at all.  Put one in a box and it collapses to zero along the box's
// axis: not a subtle bug, but not a compile error either, and every one of
// these nine appears on a migrated showcase page.
//
// What each case pins down is the SHAPE of the answer rather than the exact
// pixels -- "the label is part of the width", "the value is not part of
// anything" -- because the pixels are theme-dependent and a test that froze
// them would fail on the next font change without anything being wrong.  The
// two rules that are worth freezing, and are frozen here:
//
//   1. A hint may not depend on live process data.  A ProgressBar at 72% and
//      the same bar at 8% are the same size, or an operator watching a batch
//      run watches the row under it move all afternoon.
//   2. A hint may not depend on the geometry a previous pass gave the widget
//      (ADR-R2-09), which is why the "window onto a model" controls -- ListView
//      and ScrollArea -- report a fixed viewport rather than their contents.
//
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/IconButton.hpp"
#include "geeyoou/widget/ListView.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/RadioButton.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Separator.hpp"
#include "geeyoou/widget/Slider.hpp"
#include "geeyoou/widget/TextArea.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::IconButton;
using geeyoou::ListView;
using geeyoou::Orientation;
using geeyoou::ProgressBar;
using geeyoou::PushButton;
using geeyoou::RadioButton;
using geeyoou::ScrollArea;
using geeyoou::Separator;
using geeyoou::SizeHint;
using geeyoou::Slider;
using geeyoou::TextArea;
using geeyoou::ToggleSwitch;
using geeyoou::Widget;

namespace {
constexpr float kEps = 0.0005f;

// Every hint has to satisfy this, or a layout's arithmetic is meaningless: the
// three sizes must be ordered, and none of them negative.
bool wellFormed(const SizeHint& h) {
  return h.min.width >= 0.0f && h.min.height >= 0.0f &&
         h.preferred.width >= h.min.width && h.preferred.height >= h.min.height &&
         h.max.width >= h.preferred.width && h.max.height >= h.preferred.height;
}
}  // namespace

GEEYOOU_TEST(size_hints, separator_is_its_stroke_along_its_axis_and_free_across) {
  Separator h;
  CHECK(wellFormed(h.sizeHint()));
  // The only widget in the library with a real maximum: a 40px-tall rule is
  // 39px of nothing, taken off whatever was next to it.
  CHECK_NEAR(h.sizeHint().preferred.height, 1.0f, kEps);
  CHECK_NEAR(h.sizeHint().max.height, 1.0f, kEps);
  CHECK_NEAR(h.sizeHint().min.width, 0.0f, kEps);
  CHECK(h.sizeHint().max.width > 1000.0f);

  Separator v;
  v.setOrientation(Orientation::Vertical);
  CHECK_NEAR(v.sizeHint().preferred.width, 1.0f, kEps);
  CHECK_NEAR(v.sizeHint().max.width, 1.0f, kEps);
  CHECK(v.sizeHint().max.height > 1000.0f);

  // And it really does stay one pixel in a box that has room to spare.
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  Separator* rule = root.add<Separator>();
  box->addWidget(rule);
  box->addStretch();
  root.setGeometry({0.0f, 0.0f, 200.0f, 300.0f});
  CHECK_NEAR(rule->geometry().height(), 1.0f, kEps);
  CHECK_NEAR(rule->geometry().width(), 200.0f, kEps);
}

GEEYOOU_TEST(size_hints, a_label_is_part_of_the_width_of_the_control_it_names) {
  // The CheckBox rule, applied to the three controls that draw a caption of
  // their own: the text is not decoration, it is which pump this is.
  RadioButton r;
  const float bare = r.sizeHint().preferred.width;
  r.setText("自动");
  CHECK(r.sizeHint().preferred.width > bare);
  // ...and it does not shrink: min == preferred across, because a radio narrowed
  // past its caption does not reflow, it truncates.
  CHECK_NEAR(r.sizeHint().min.width, r.sizeHint().preferred.width, kEps);
  CHECK(wellFormed(r.sizeHint()));

  ToggleSwitch s;
  const float pill = s.sizeHint().preferred.width;
  s.setText("进料泵");
  CHECK(s.sizeHint().preferred.width > pill);
  CHECK_NEAR(s.sizeHint().min.width, s.sizeHint().preferred.width, kEps);
  CHECK(wellFormed(s.sizeHint()));

  // A radio and a check box stacked in one column have to agree on the row
  // height, or the group reads as a rendering fault.
  geeyoou::CheckBox c;
  CHECK_NEAR(r.sizeHint().preferred.height, c.sizeHint().preferred.height, kEps);
}

GEEYOOU_TEST(size_hints, the_live_value_is_never_part_of_the_size) {
  // Rule 1.  A slider that re-measured itself as it was dragged would re-flow
  // the row under the operator's finger; a progress bar that re-measured itself
  // as a batch ran would do it all afternoon.
  Slider s;
  const SizeHint before = s.sizeHint();
  s.setRange(0.0, 15000.0);
  s.setValue(9999.0);
  s.setTickCount(11);
  CHECK_NEAR(s.sizeHint().preferred.width, before.preferred.width, kEps);
  CHECK_NEAR(s.sizeHint().preferred.height, before.preferred.height, kEps);
  CHECK(wellFormed(before));
  // Tall enough for the handle (radius 8, +1 while dragged) and the tick marks
  // (7px below a track that sits 2.5px below the centre).
  CHECK(before.min.height >= 19.0f);

  ProgressBar p;
  const SizeHint pb = p.sizeHint();
  p.setValue(72.0);
  p.setText("料位 72%");
  CHECK_NEAR(p.sizeHint().preferred.width, pb.preferred.width, kEps);
  CHECK_NEAR(p.sizeHint().preferred.height, pb.preferred.height, kEps);
  CHECK(wellFormed(pb));
  // Hiding the text lets it get thinner, because now there is nothing in it.
  p.setTextVisible(false);
  CHECK(p.sizeHint().min.height < pb.min.height);
}

GEEYOOU_TEST(size_hints, a_text_area_asks_for_rows_not_for_its_text) {
  // Rule 2, the circular half: the number of wrapped rows is a function of the
  // width the layout has not decided yet.  So the hint is a row count, and a
  // form that wants a taller field gives it stretch.
  TextArea t;
  const SizeHint empty = t.sizeHint();
  CHECK(wellFormed(empty));
  t.setText(
      "1. 08:20 进料泵 P-101 启动。\n2. 09:05 到达设定值。\n"
      "3. 10:30 泄压阀手动排空。\n4. 11:00 巡检未发现异常。\n5. 交接完毕。");
  CHECK_NEAR(t.sizeHint().preferred.height, empty.preferred.height, kEps);
  CHECK_NEAR(t.sizeHint().preferred.width, empty.preferred.width, kEps);
  CHECK(t.sizeHint().preferred.height > t.sizeHint().min.height);
}

GEEYOOU_TEST(size_hints, an_icon_button_is_square_and_as_tall_as_the_buttons_beside_it) {
  IconButton b;
  b.setIcon(geeyoou::Icon::Play);
  const SizeHint h = b.sizeHint();
  CHECK(wellFormed(h));
  CHECK_NEAR(h.preferred.width, h.preferred.height, kEps);
  CHECK_NEAR(h.min.width, h.min.height, kEps);

  // The dimension that has to agree with the push buttons in the same toolbar.
  PushButton p;
  p.setText("保存");
  CHECK_NEAR(h.preferred.height, p.sizeHint().preferred.height, kEps);
  // ...and it is NOT as wide as that button, which is the whole point: the base
  // class's width is a label plus padding, and this control draws no label.
  CHECK(h.preferred.width < p.sizeHint().preferred.width);
}

GEEYOOU_TEST(size_hints, a_view_onto_a_model_reports_a_viewport_not_the_model) {
  // Rule 2 again, and the one that would have been genuinely dangerous: the
  // alarm list holds 2000 rows.  A hint of "as tall as my rows" is 52000
  // logical pixels, which the enclosing layout would faithfully report as a
  // 51000-pixel overflow -- a real number, describing nothing wrong.
  ListView v;
  v.setColumns({{"时间", 150.0f}, {"位号", 90.0f}, {"内容", 0.0f}});
  const SizeHint empty = v.sizeHint();
  CHECK(wellFormed(empty));
  v.setRowCount(2000);
  CHECK_NEAR(v.sizeHint().preferred.height, empty.preferred.height, kEps);
  CHECK(v.sizeHint().preferred.height < 400.0f);
  // The COLUMNS are a genuine statement about the width, so they do count.
  v.setColumns({{"时间", 150.0f}});
  CHECK(v.sizeHint().preferred.width < empty.preferred.width);
  // Taller rows, taller view -- that is configuration, not data.
  const float atDefault = v.sizeHint().preferred.height;
  v.setRowHeight(40.0f);
  CHECK(v.sizeHint().preferred.height > atDefault);

  ScrollArea s;
  const SizeHint sh = s.sizeHint();
  CHECK(wellFormed(sh));
  s.setContentSize({4000.0f, 9000.0f});
  CHECK_NEAR(s.sizeHint().preferred.width, sh.preferred.width, kEps);
  CHECK_NEAR(s.sizeHint().preferred.height, sh.preferred.height, kEps);
}

GEEYOOU_TEST(size_hints, every_new_hint_survives_being_put_in_a_box) {
  // The end-to-end version of the bug this file exists to prevent: before these
  // nine hints, each of these controls came out of a vertical box with a height
  // of zero.
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  box->setSpacing(4.0f);

  Widget* kids[7] = {
      root.add<RadioButton>(), root.add<ToggleSwitch>(), root.add<Slider>(),
      root.add<ProgressBar>(), root.add<TextArea>(),     root.add<IconButton>(),
      root.add<ListView>(),
  };
  for (Widget* k : kids) box->addWidget(k);
  root.setGeometry({0.0f, 0.0f, 400.0f, 900.0f});

  for (Widget* k : kids) {
    CHECK(k->geometry().height() > 0.0f);
    CHECK(k->geometry().width() > 0.0f);
  }
  CHECK(!root.lastLayoutOverflow().any());
}
