#pragma once
//
// The demo data every table page shares, and the two small classes that make a
// page readable instead of a wall of column literals.
//
// WHY A MODEL CLASS AND NOT LAMBDAS.  FunctionTableModel exists and would work,
// but the showcase is also the worked example: a real screen has a record type
// it already owns, and the model's whole job is to answer questions ABOUT that
// record type without copying it.  DeviceModel below holds a POINTER to the
// application's vector and a column map -- nothing else.  Ten tables on seven
// pages read the same 60 records.
//
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/TableModel.hpp"
#include "geeyoou/widget/TablePager.hpp"
#include "geeyoou/widget/TableView.hpp"
#include "geeyoou/widget/TreeTableModel.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using geeyoou::CellAction;
using geeyoou::CellKind;
using geeyoou::CellSpan;
using geeyoou::Color;
using geeyoou::Rect;
using geeyoou::SelectItem;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::TableModel;
using geeyoou::TablePager;
using geeyoou::TableView;
using geeyoou::Theme;
using geeyoou::Widget;

// One row of the plant's instrument register.
struct Device {
  std::string tag;
  std::string name;
  std::string area;
  std::string type;
  std::string status;  // 运行 / 停机 / 维护 / 故障
  std::string tags;    // comma separated, edited by the in-cell multi-select
  double progress = 0.0;
  double range = 0.0;
  bool running = false;
  bool picked = false;
};

// WHICH FIELD a view column shows.  A view column index is NOT a record field
// index -- a page that shows five of the eleven fields would otherwise have to
// renumber the model, and the next page would renumber it back.
enum class Field {
  Selector,  // row selection, drawn by the view, no field behind it
  Index,
  Tag,
  Name,
  Area,
  Type,
  Status,
  Tags,
  Progress,
  Range,
  Running,
  Picked,
  Actions,
};

// Sixty instruments across four areas.  Built on first use and handed out by
// reference: the tables never copy it, which is the property the whole design
// is about.
//
// REBUILT when the language changes.  It used to be a `static ... = [] {...}()`
// that ran exactly once, which is the same trap as a namespace-scope const
// array: the rows would keep whatever language was current the first time a
// table page was opened, and rebuilding the PAGE would not touch them.
//
// The vector OBJECT is reused rather than replaced, so the reference every
// caller holds stays valid; only the contents change.  Callers are being
// rebuilt at that moment anyway, so nobody is mid-iteration.
inline std::vector<Device>& demoDevices() {
  static std::vector<Device> rows;
  static int builtFor = -1;
  if (builtFor == lang()) return rows;
  builtFor = lang();

  const std::string areas[] = {tr("一号反应区"), tr("二号反应区"),
                               tr("罐区"), tr("公用工程")};
  const std::string types[] = {tr("温度"), tr("压力"), tr("流量"), tr("液位"),
                               tr("阀门")};
  const std::string states[] = {tr("运行"), tr("停机"), tr("维护"), tr("故障")};
  const std::string tagSets[] = {tr("关键"), tr("关键, 联锁"), tr("常规"),
                                 tr("常规, 备用"), tr("联锁")};

  rows.clear();
  rows.reserve(60);
  for (int i = 0; i < 60; ++i) {
    Device d;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s-%03d", i % 2 ? "PI" : "TI", 101 + i);
    d.tag = buf;
    d.type = types[i % 5];
    d.name = d.type + tr("变送器 ") + std::to_string(101 + i);
    d.area = areas[(i / 15) % 4];
    // Deliberately uneven: three quarters running is what a plant looks like,
    // and a demo where every row is green shows nothing about the colours.
    d.status = states[(i * 7) % 11 < 7 ? 0 : (i % 3 + 1)];
    d.running = d.status == tr("运行");
    d.progress = double((i * 17) % 101) / 100.0;
    d.range = 20.0 + double((i * 13) % 380);
    d.tags = tagSets[i % 5];
    rows.push_back(std::move(d));
  }
  return rows;
}

// The colour a status chip is drawn in.  Named by MEANING here rather than in
// the model, so a plant that recolours "维护" changes one function.
inline Color statusColor(const std::string& s) {
  const Theme& t = Theme::current();
  if (s == tr("运行")) return t.success;
  if (s == tr("故障")) return t.danger;
  if (s == tr("维护")) return t.warn;
  return t.textDim;
}

// A tree whose chip colour is DERIVED from the row's status text,每次绘制时求值.
//
// Same argument as CellAction::Tone one level up: TreeTableModel::setAccent
// stores a Color, and a demo that called it with Theme::current().success at
// build time would freeze that skin into the model.  Overriding the accessor
// costs three lines and is live under every skin.
//
// `statusCol` is which of the node's cells holds the status word.
class StatusTreeModel : public geeyoou::TreeTableModel {
 public:
  explicit StatusTreeModel(int statusCol) : statusCol_(statusCol) {}

  Color tableAccent(int row, int col) const override {
    if (col < 0) return Color::rgba(0, 0, 0, 0);
    const std::string s = tableText(row, statusCol_);
    return s.empty() ? Color::rgba(0, 0, 0, 0) : statusColor(s);
  }

 private:
  int statusCol_ = 0;
};

// A pull model over `rows`, showing `fields` in that order.
//
// The window (offset/count) is what paging moves.  Note that it is the MODEL
// that gets a window, not the view: the view virtualises whatever it is given
// and cannot tell a 20-row page from a 60-row table.
class DeviceModel : public TableModel {
 public:
  DeviceModel(std::vector<Device>* rows, std::vector<Field> fields)
      : rows_(rows), fields_(std::move(fields)) {}

  void setWindow(int offset, int count) {
    offset_ = offset;
    count_ = count;
  }
  void clearWindow() {
    offset_ = 0;
    count_ = -1;
  }
  // Merge the area column: every run of rows sharing an area becomes one cell.
  void setMergeArea(bool on) { mergeArea_ = on; }

  Device* deviceAt(int row) {
    const int i = offset_ + row;
    if (!rows_ || i < 0 || i >= int(rows_->size())) return nullptr;
    return &(*rows_)[std::size_t(i)];
  }
  const Device* deviceAt(int row) const {
    return const_cast<DeviceModel*>(this)->deviceAt(row);
  }

  int tableRowCount() const override {
    if (!rows_) return 0;
    const int total = int(rows_->size());
    if (count_ < 0) return total;
    return std::max(0, std::min(count_, total - offset_));
  }

  std::string tableText(int row, int col) const override {
    const Device* d = deviceAt(row);
    if (!d) return {};
    switch (field(col)) {
      case Field::Tag: return d->tag;
      case Field::Name: return d->name;
      case Field::Area: return d->area;
      case Field::Type: return d->type;
      case Field::Status: return d->status;
      case Field::Tags: return d->tags;
      case Field::Range: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", d->range);
        return buf;
      }
      case Field::Progress: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", d->progress * 100.0);
        return buf;
      }
      default: return {};
    }
  }

  double tableNumber(int row, int col) const override {
    const Device* d = deviceAt(row);
    if (!d) return 0.0;
    return field(col) == Field::Progress ? d->progress : 0.0;
  }

  bool tableFlag(int row, int col) const override {
    const Device* d = deviceAt(row);
    if (!d) return false;
    switch (field(col)) {
      case Field::Running: return d->running;
      case Field::Picked: return d->picked;
      default: return false;
    }
  }

  Color tableAccent(int row, int col) const override {
    const Device* d = deviceAt(row);
    if (!d) return Color::rgba(0, 0, 0, 0);
    // col < 0 is the row's severity bar.  Only a fault earns one: a bar on every
    // row is a bar that says nothing.
    if (col < 0) {
      return d->status == tr("故障") ? Theme::current().danger : Color::rgba(0, 0, 0, 0);
    }
    return field(col) == Field::Status ? statusColor(d->status)
                                       : Color::rgba(0, 0, 0, 0);
  }

  // The area column merges over the run of rows that share an area.  An anchor
  // answers with its own extent; every row below it answers with the NEGATIVE
  // offset back to that anchor, which is what keeps this O(1) per cell.
  CellSpan tableSpan(int row, int col) const override {
    if (!mergeArea_ || field(col) != Field::Area) return {};
    const Device* d = deviceAt(row);
    if (!d) return {};

    int first = row;
    while (first > 0) {
      const Device* up = deviceAt(first - 1);
      if (!up || up->area != d->area) break;
      --first;
    }
    if (first != row) return {first - row, 0};  // covered: point at the anchor

    int last = row;
    const int n = tableRowCount();
    while (last + 1 < n) {
      const Device* dn = deviceAt(last + 1);
      if (!dn || dn->area != d->area) break;
      ++last;
    }
    return {last - first + 1, 1};
  }

  bool tableSetText(int row, int col, const std::string& value) override {
    Device* d = deviceAt(row);
    if (!d) return false;
    switch (field(col)) {
      case Field::Tag: d->tag = value; return true;
      case Field::Name: d->name = value; return true;
      case Field::Type: d->type = value; return true;
      case Field::Status: d->status = value; return true;
      case Field::Tags: d->tags = value; return true;
      case Field::Range: d->range = std::atof(value.c_str()); return true;
      default: return false;
    }
  }

  bool tableSetFlag(int row, int col, bool on) override {
    Device* d = deviceAt(row);
    if (!d) return false;
    switch (field(col)) {
      case Field::Running:
        d->running = on;
        // The status chip is DERIVED from the switch, so flipping one has to
        // move the other -- two cells showing contradictory truths is the
        // classic symptom of state kept in two places.
        d->status = on ? tr("运行") : tr("停机");
        return true;
      case Field::Picked: d->picked = on; return true;
      default: return false;
    }
  }

 private:
  Field field(int col) const {
    return (col >= 0 && col < int(fields_.size())) ? fields_[std::size_t(col)]
                                                   : Field::Index;
  }

  std::vector<Device>* rows_ = nullptr;
  std::vector<Field> fields_;
  int offset_ = 0;
  int count_ = -1;
  bool mergeArea_ = false;
};

// Holds a model, and exists ONLY for where it sits in TablePanel's base list.
//
// ⚠️ THIS IS NOT A STYLE CHOICE.  It is the fix for a real defect that the ASan
// leg of verify.bat caught, and the shape of the fix is the whole lesson:
//
//   A TableView holds a RAW pointer to its model, so the table has to die BEFORE
//   the model does.  Making the model a plain member of a Widget subclass gets
//   that exactly backwards -- members are destroyed BEFORE base classes, so the
//   model would go first and ~Widget would then destroy a table pointing at it.
//
//   The first attempt at a fix was a destructor body that called
//   `removeChild(table_)` to destroy the reader by hand.  IT CRASHED, and the
//   report is worth remembering: removeChild is a tree operation, it marks the
//   layout chain dirty and a pass runs -- on an ancestor chain that is ALREADY
//   HALFWAY THROUGH BEING DESTROYED, because this panel is only being destructed
//   at all as part of its parent's teardown.  Access violation in
//   Layout::arrangeFor, three frames above ~Widget.
//
//   So: DO NOT TOUCH THE TREE FROM A DESTRUCTOR.  Get the order from the
//   language instead.  Destruction runs body -> members -> bases in REVERSE
//   declaration order, so with the holder declared FIRST and Widget SECOND,
//   ~Widget runs first (destroying the child table) and ~ModelHolder second
//   (destroying the model).  The reader dies before the thing it reads, by
//   construction rather than by anybody remembering.
class TableModelHolder {
 protected:
  std::unique_ptr<TableModel> model_;
};

// A table, the model it reads, and optionally a pager under it.
class TablePanel : public TableModelHolder, public Widget {
 public:
  explicit TablePanel(std::unique_ptr<TableModel> model, bool withPager = false) {
    model_ = std::move(model);
    table_ = add<TableView>();
    table_->setModel(model_.get());
    if (withPager) pager_ = add<TablePager>();
  }

  // No destructor at all, on purpose.  See the note on TableModelHolder: the
  // ordering this class needs is now a property of its base list, and any body
  // written here would be a second, weaker statement of the same thing.

  TableView* table() const { return table_; }
  TablePager* pager() const { return pager_; }
  TableModel* model() const { return model_.get(); }

  void setDesignHeight(float px) {
    designHeight_ = px;
    invalidateSizeHint();
  }

  SizeHint sizeHint() const override {
    SizeHint h;
    h.preferred = Size{560.0f, designHeight_};
    h.min = Size{320.0f, 140.0f};
    return h;
  }

 protected:
  void onGeometryChanged() override {
    const Rect r = localRect();
    const float pagerH = pager_ ? 44.0f : 0.0f;
    if (table_) {
      table_->setGeometry({0.0f, 0.0f, r.width(),
                           std::max(0.0f, r.height() - pagerH)});
    }
    if (pager_) {
      pager_->setGeometry({0.0f, std::max(0.0f, r.height() - pagerH), r.width(),
                           pagerH});
    }
  }

 private:
  TableView* table_ = nullptr;
  TablePager* pager_ = nullptr;
  float designHeight_ = 320.0f;
};

// The column literals every page would otherwise repeat.
inline TableView::Column textColumn(std::string title, float width,
                                    bool sortable = false) {
  TableView::Column c;
  c.title = std::move(title);
  c.width = width;
  c.sortable = sortable;
  return c;
}

inline TableView::Column kindColumn(std::string title, float width, CellKind kind) {
  TableView::Column c;
  c.title = std::move(title);
  c.width = width;
  c.kind = kind;
  if (kind == CellKind::Index || kind == CellKind::Selector ||
      kind == CellKind::Check || kind == CellKind::Switch) {
    c.align = geeyoou::HAlign::Center;
  }
  return c;
}

}  // namespace showcase
