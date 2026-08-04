#include "geeyoou/widget/TreeSelect.hpp"

#include <algorithm>

namespace geeyoou {

void TreeSelect::setRoots(std::vector<TreeItem> roots) {
  roots_ = std::move(roots);
  flat_.clear();
  if (isOpen()) refreshRows();
  update();
}

void TreeSelect::setLeafOnly(bool on) {
  leafOnly_ = on;
  if (isOpen()) refreshRows();
}

void TreeSelect::setShowPath(bool on) {
  showPath_ = on;
  update();
}

void TreeSelect::setExpandedAll(std::vector<TreeItem>& nodes, bool on) {
  for (TreeItem& n : nodes) {
    n.expanded = on;
    setExpandedAll(n.children, on);
  }
}

void TreeSelect::expandAll() {
  setExpandedAll(roots_, true);
  if (isOpen()) refreshRows();
}

void TreeSelect::collapseAll() {
  setExpandedAll(roots_, false);
  if (isOpen()) refreshRows();
}

TreeItem* TreeSelect::nodeAt(const std::vector<int>& path) {
  std::vector<TreeItem>* level = &roots_;
  TreeItem* node = nullptr;
  for (int idx : path) {
    if (idx < 0 || idx >= int(level->size())) return nullptr;
    node = &(*level)[std::size_t(idx)];
    level = &node->children;
  }
  return node;
}

bool TreeSelect::findValue(std::vector<TreeItem>& nodes, const std::string& value,
                           std::vector<std::string>& trail) {
  for (TreeItem& n : nodes) {
    trail.push_back(n.text);
    const std::string v = n.value.empty() ? n.text : n.value;
    if (v == value && (!leafOnly_ || n.isLeaf())) return true;
    if (findValue(n.children, value, trail)) {
      n.expanded = true;  // reveal the path to a programmatically set value
      return true;
    }
    trail.pop_back();
  }
  return false;
}

void TreeSelect::setSelectedValue(const std::string& value) {
  std::vector<std::string> trail;
  if (!findValue(roots_, value, trail)) return;
  selectedValue_ = value;
  selectedText_ = trail.empty() ? std::string{} : trail.back();
  if (showPath_ && trail.size() > 1) {
    std::string joined;
    for (std::size_t i = 0; i < trail.size(); ++i) {
      if (i) joined += " / ";
      joined += trail[i];
    }
    selectedText_ = joined;
  }
  if (isOpen()) refreshRows();
  update();
  selectionChanged.emit(selectedValue_);
}

std::string TreeSelect::displayText() const { return selectedText_; }

void TreeSelect::flatten(std::vector<TreeItem>& nodes, std::vector<int>& path,
                         int depth, std::vector<PopupRow>& rows) {
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    TreeItem& n = nodes[i];
    path.push_back(int(i));

    PopupRow r;
    r.text = n.text;
    r.secondary = n.secondary;
    r.modelIndex = int(rows.size());
    r.indent = depth;
    r.expandable = !n.isLeaf();
    r.expanded = n.expanded;
    r.icon = n.icon;
    // A branch stays clickable even in leaf-only mode -- the click expands it,
    // which is friendlier than an inert row the operator keeps poking at.
    r.enabled = n.enabled;
    const std::string v = n.value.empty() ? n.text : n.value;
    r.selected = (!selectedValue_.empty() && v == selectedValue_ &&
                  (!leafOnly_ || n.isLeaf()));
    rows.push_back(std::move(r));

    FlatRef ref;
    ref.path = path;
    ref.leaf = n.isLeaf();
    flat_.push_back(std::move(ref));

    if (n.expanded && !n.isLeaf()) flatten(n.children, path, depth + 1, rows);
    path.pop_back();
  }
}

std::vector<PopupRow> TreeSelect::buildRows() {
  flat_.clear();
  std::vector<PopupRow> rows;
  std::vector<int> path;
  flatten(roots_, path, 0, rows);
  return rows;
}

void TreeSelect::onExpanderToggled(int row) {
  if (row < 0 || row >= int(flat_.size())) return;
  TreeItem* n = nodeAt(flat_[std::size_t(row)].path);
  if (!n || n->isLeaf()) return;
  n->expanded = !n->expanded;
  refreshRows();
  // The row index is still valid for the node itself: expanding only inserts
  // rows AFTER it, and collapsing only removes rows after it.
  list()->setHighlighted(row, false);
}

void TreeSelect::onRowActivated(int row) {
  if (row < 0 || row >= int(flat_.size())) return;
  const bool leaf = flat_[std::size_t(row)].leaf;
  TreeItem* n = nodeAt(flat_[std::size_t(row)].path);
  if (!n || !n->enabled) return;

  if (leafOnly_ && !leaf) {
    onExpanderToggled(row);
    return;
  }

  // Rebuild the display path from the node chain we already have.
  std::vector<std::string> trail;
  {
    std::vector<int> partial;
    for (int idx : flat_[std::size_t(row)].path) {
      partial.push_back(idx);
      if (TreeItem* step = nodeAt(partial)) trail.push_back(step->text);
    }
  }

  selectedValue_ = n->value.empty() ? n->text : n->value;
  selectedText_ = n->text;
  if (showPath_ && trail.size() > 1) {
    std::string joined;
    for (std::size_t i = 0; i < trail.size(); ++i) {
      if (i) joined += " / ";
      joined += trail[i];
    }
    selectedText_ = joined;
  }
  update();
  selectionChanged.emit(selectedValue_);
}

}  // namespace geeyoou
