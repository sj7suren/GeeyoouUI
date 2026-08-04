#include "geeyoou/widget/Cascader.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr int kMaxColumns = 5;  // deeper than this and a cascader is the wrong
                                // control -- use TreeSelect
}

void Cascader::setRoots(std::vector<TreeItem> roots) {
  roots_ = std::move(roots);
  clearSelection();
}

void Cascader::setColumnWidth(float w) { columnWidth_ = std::max(80.0f, w); }

void Cascader::setMaxVisibleRows(int n) {
  maxRows_ = std::max(1, n);
  for (PopupList* c : columns_) c->setMaxVisibleRows(maxRows_);
}

void Cascader::clearSelection() {
  picks_.clear();
  pathTexts_.clear();
  value_.clear();
  update();
}

std::string Cascader::displayText() const {
  if (pathTexts_.empty()) return {};
  std::string out;
  for (std::size_t i = 0; i < pathTexts_.size(); ++i) {
    if (i) out += " / ";
    out += pathTexts_[i];
  }
  return out;
}

// Nodes shown in column `level`: the roots for level 0, otherwise the children
// of whatever is picked in the column to its left.
std::vector<TreeItem>* Cascader::levelNodes(int level) {
  std::vector<TreeItem>* nodes = &roots_;
  for (int i = 0; i < level; ++i) {
    if (i >= int(picks_.size())) return nullptr;
    const int idx = picks_[std::size_t(i)];
    if (idx < 0 || idx >= int(nodes->size())) return nullptr;
    nodes = &(*nodes)[std::size_t(idx)].children;
    if (nodes->empty()) return nullptr;
  }
  return nodes;
}

void Cascader::chooseAt(int level, int rowIndex) {
  // Picking in a column invalidates everything to its right.
  picks_.resize(std::size_t(level));
  picks_.push_back(rowIndex);

  // Rebuild the display path from the picks.
  pathTexts_.clear();
  std::vector<TreeItem>* nodes = &roots_;
  TreeItem* leaf = nullptr;
  for (std::size_t i = 0; i < picks_.size(); ++i) {
    const int idx = picks_[i];
    if (!nodes || idx < 0 || idx >= int(nodes->size())) break;
    leaf = &(*nodes)[std::size_t(idx)];
    pathTexts_.push_back(leaf->text);
    nodes = &leaf->children;
  }

  const bool atLeaf = leaf && leaf->isLeaf();
  if (leaf && (!leafOnly_ || atLeaf)) {
    value_ = leaf->value.empty() ? leaf->text : leaf->value;
    selectionChanged.emit(value_);
  } else {
    value_.clear();
  }

  rebuildColumns();
  update();

  // A leaf ends the interaction; a branch just opens the next column.
  if (atLeaf) close();
}

void Cascader::rebuildColumns() {
  if (!popupBox_) return;
  const Theme& t = Theme::current();

  // How many columns are meaningful right now: one per pick, plus one more if
  // the deepest pick still has children.
  int wanted = 1;
  for (int level = 1; level < kMaxColumns; ++level) {
    if (!levelNodes(level)) break;
    wanted = level + 1;
  }

  for (int level = 0; level < wanted; ++level) {
    if (level >= int(columns_.size())) {
      PopupList* col = popupBox_->add<PopupList>();
      col->setMaxVisibleRows(maxRows_);
      col->setVisible(true);
      const int captured = level;
      col->rowActivated.connect([this, captured](int row) {
        const auto& rows = columns_[std::size_t(captured)]->rows();
        if (row >= 0 && row < int(rows.size())) {
          chooseAt(captured, rows[std::size_t(row)].modelIndex);
        }
      });
      columns_.push_back(col);
    }

    PopupList* col = columns_[std::size_t(level)];
    std::vector<TreeItem>* nodes = levelNodes(level);
    std::vector<PopupRow> rows;
    if (nodes) {
      rows.reserve(nodes->size());
      for (std::size_t i = 0; i < nodes->size(); ++i) {
        const TreeItem& n = (*nodes)[i];
        PopupRow r;
        r.text = n.text;
        r.secondary = n.secondary;
        r.modelIndex = int(i);
        r.enabled = n.enabled;
        r.icon = n.icon;
        r.selected = (level < int(picks_.size()) && picks_[std::size_t(level)] == int(i));
        // A branch gets a chevron so it is obvious another column will open.
        if (!n.isLeaf()) r.shortcutText = ">";
        rows.push_back(std::move(r));
      }
    }
    col->setRows(std::move(rows));
    col->setVisible(true);
  }

  // Columns beyond the meaningful depth are hidden, not destroyed -- recreating
  // them on every pick would churn Window children for nothing.
  for (std::size_t i = std::size_t(wanted); i < columns_.size(); ++i) {
    columns_[i]->setVisible(false);
  }

  // Lay the visible columns out side by side and size the container to fit.
  float x = 0.0f;
  float maxH = 0.0f;
  for (int level = 0; level < wanted; ++level) {
    PopupList* col = columns_[std::size_t(level)];
    const float h = col->preferredHeight();
    col->setGeometry({x, 0.0f, columnWidth_, h});
    x += columnWidth_ + 1.0f;
    maxH = std::max(maxH, h);
  }
  for (int level = 0; level < wanted; ++level) {
    // Equal heights: ragged column bottoms read as a rendering bug.
    const Rect g = columns_[std::size_t(level)]->geometry();
    columns_[std::size_t(level)]->setGeometry({g.x(), g.y(), g.width(), maxH});
  }
  popupBox_->setGeometry({popupBox_->geometry().x(), popupBox_->geometry().y(),
                          std::max(1.0f, x - 1.0f), maxH});
  (void)t;
}

void Cascader::open() {
  if (!isEffectivelyEnabled() || isOpen() || roots_.empty()) return;
  Window* w = window();
  if (!w) return;

  if (!popupBox_) {
    popupBox_ = w->add<Widget>();
    popupBox_->setVisible(false);
  }
  rebuildColumns();
  showCustomPopup(popupBox_);
}

void Cascader::close() {
  if (popupBox_) hideCustomPopup(popupBox_);
}

}  // namespace geeyoou
