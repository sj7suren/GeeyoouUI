#include "geeyoou/widget/TreeTableModel.hpp"

#include <algorithm>

namespace geeyoou {
namespace {
const std::vector<std::string> kNoCells;
const std::vector<TreeTableModel::NodeId> kNoChildren;
}  // namespace

// Node 0 is the invisible root, created here so that addNode(kRootNode, ...) is
// the only way anything is ever added.  Its depth is -1, which makes a real root
// come out at 0 without the flattener special-casing anything.
TreeTableModel::TreeTableModel() {
  Node root;
  root.depth = -1;
  root.expanded = true;
  nodes_.push_back(std::move(root));
  rowOf_.push_back(-1);
}

TreeTableModel::NodeId TreeTableModel::addNode(NodeId parent,
                                               std::vector<std::string> cells) {
  if (!valid(parent)) return kInvalidNode;

  Node n;
  n.cells = std::move(cells);
  n.parent = parent;
  n.depth = nodes_[std::size_t(parent)].depth + 1;

  const NodeId id = NodeId(nodes_.size());
  nodes_.push_back(std::move(n));
  rowOf_.push_back(-1);
  nodes_[std::size_t(parent)].children.push_back(id);

  // A branch that was Pending and has just been given a child is no longer
  // waiting for anything -- this is the path an application takes when it
  // supplies children WITHOUT going through finishLoad, and leaving the state at
  // Pending would draw a collapsed expander over children that are already here.
  Node& p = nodes_[std::size_t(parent)];
  if (p.state == LoadState::Pending) p.state = LoadState::None;

  rebuildVisible();
  return id;
}

void TreeTableModel::setNumber(NodeId node, int col, double v) {
  if (!valid(node) || col < 0) return;
  Node& n = nodes_[std::size_t(node)];
  if (int(n.numbers.size()) <= col) n.numbers.resize(std::size_t(col) + 1, 0.0);
  n.numbers[std::size_t(col)] = v;
}

void TreeTableModel::setFlag(NodeId node, int col, bool on) {
  if (!valid(node) || col < 0) return;
  Node& n = nodes_[std::size_t(node)];
  if (int(n.flags.size()) <= col) n.flags.resize(std::size_t(col) + 1, 0);
  n.flags[std::size_t(col)] = on ? 1 : 0;
}

void TreeTableModel::setAccent(NodeId node, Color c) {
  if (!valid(node)) return;
  nodes_[std::size_t(node)].accent = c;
}

void TreeTableModel::setLazy(NodeId node, bool on) {
  if (!valid(node)) return;
  nodes_[std::size_t(node)].state = on ? LoadState::Pending : LoadState::None;
  rebuildVisible();
}

void TreeTableModel::clear() {
  nodes_.resize(1);
  nodes_[0].children.clear();
  rowOf_.assign(1, -1);
  rebuildVisible();
  structureChanged.emit();
}

// The emit is last, as everywhere else in this library that a mutator ends in
// one: a slot is entitled to destroy things, and there is nothing after it here
// that would read them.
void TreeTableModel::finishLoad(NodeId node, bool ok) {
  if (!valid(node)) return;
  Node& n = nodes_[std::size_t(node)];
  n.state = ok ? LoadState::Ready : LoadState::Failed;
  // Opening on success is the whole point of the interaction: the operator asked
  // for what is under this branch, and making them click a second time to see
  // what they just waited for is a design that has forgotten what it is doing.
  if (ok) n.expanded = true;
  rebuildVisible();
  structureChanged.emit();
}

TreeTableModel::LoadState TreeTableModel::loadState(NodeId node) const {
  return valid(node) ? nodes_[std::size_t(node)].state : LoadState::None;
}

void TreeTableModel::setExpanded(NodeId node, bool on) {
  if (!valid(node) || nodes_[std::size_t(node)].expanded == on) return;
  nodes_[std::size_t(node)].expanded = on;
  rebuildVisible();
  structureChanged.emit();
}

bool TreeTableModel::isExpanded(NodeId node) const {
  return valid(node) && nodes_[std::size_t(node)].expanded;
}

void TreeTableModel::expandAll() {
  for (Node& n : nodes_) {
    // A Pending branch is NOT opened: expanding it would show an empty node
    // where the operator expects contents, and firing a fetch per branch from
    // one menu click is how an "expand all" becomes a denial of service against
    // the plant's own historian.
    if (n.state != LoadState::Pending) n.expanded = true;
  }
  rebuildVisible();
  structureChanged.emit();
}

void TreeTableModel::collapseAll() {
  for (std::size_t i = 1; i < nodes_.size(); ++i) nodes_[i].expanded = false;
  rebuildVisible();
  structureChanged.emit();
}

TreeTableModel::NodeId TreeTableModel::nodeAtRow(int row) const {
  if (row < 0 || row >= int(visible_.size())) return kInvalidNode;
  return visible_[std::size_t(row)];
}

int TreeTableModel::rowOfNode(NodeId node) const {
  return valid(node) ? rowOf_[std::size_t(node)] : -1;
}

const std::vector<std::string>& TreeTableModel::cellsOf(NodeId node) const {
  return valid(node) ? nodes_[std::size_t(node)].cells : kNoCells;
}

TreeTableModel::NodeId TreeTableModel::parentOf(NodeId node) const {
  return valid(node) ? nodes_[std::size_t(node)].parent : kInvalidNode;
}

const std::vector<TreeTableModel::NodeId>& TreeTableModel::childrenOf(
    NodeId node) const {
  return valid(node) ? nodes_[std::size_t(node)].children : kNoChildren;
}

// ---------------------------------------------------------------- flatten ---
//
// Recursive, and bounded by the tree's own depth rather than by kMaxTreeDepth --
// this is a data structure, not the widget tree.  An equipment hierarchy that
// were deep enough to matter here would be unreadable long before it were
// dangerous, and the alternative (an explicit stack) buys nothing but a longer
// function.
void TreeTableModel::appendSubtree(NodeId node) {
  for (NodeId child : nodes_[std::size_t(node)].children) {
    rowOf_[std::size_t(child)] = int(visible_.size());
    visible_.push_back(child);
    if (nodes_[std::size_t(child)].expanded) appendSubtree(child);
  }
}

void TreeTableModel::rebuildVisible() {
  visible_.clear();
  rowOf_.assign(nodes_.size(), -1);
  appendSubtree(kRootNode);
}

// ------------------------------------------------------------- TableModel ---
std::string TreeTableModel::tableText(int row, int col) const {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode || col < 0) return {};
  const Node& n = nodes_[std::size_t(id)];
  if (col >= int(n.cells.size())) return {};
  return n.cells[std::size_t(col)];
}

double TreeTableModel::tableNumber(int row, int col) const {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode || col < 0) return 0.0;
  const Node& n = nodes_[std::size_t(id)];
  if (col >= int(n.numbers.size())) return 0.0;
  return n.numbers[std::size_t(col)];
}

bool TreeTableModel::tableFlag(int row, int col) const {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode || col < 0) return false;
  const Node& n = nodes_[std::size_t(id)];
  if (col >= int(n.flags.size())) return false;
  return n.flags[std::size_t(col)] != 0;
}

// The node's accent tints its CHIP cells.  It deliberately does NOT become the
// row's leading severity bar (col < 0): a bar on every row is a bar that says
// nothing, and a tree whose every node is coloured is a tree nobody can scan.
Color TreeTableModel::tableAccent(int row, int col) const {
  if (col < 0) return Color::rgba(0, 0, 0, 0);
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode) return Color::rgba(0, 0, 0, 0);
  return nodes_[std::size_t(id)].accent;
}

int TreeTableModel::tableDepth(int row) const {
  const NodeId id = nodeAtRow(row);
  return id == kInvalidNode ? 0 : nodes_[std::size_t(id)].depth;
}

RowExpansion TreeTableModel::tableExpansion(int row) const {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode) return RowExpansion::Leaf;
  const Node& n = nodes_[std::size_t(id)];

  switch (n.state) {
    case LoadState::Loading: return RowExpansion::Loading;
    case LoadState::Failed:  return RowExpansion::Failed;
    case LoadState::Pending: return RowExpansion::Collapsed;
    default: break;
  }
  if (n.children.empty()) return RowExpansion::Leaf;
  return n.expanded ? RowExpansion::Expanded : RowExpansion::Collapsed;
}

// Called by TableView from a MOUSE handler, never from a paint -- which is the
// licence this one function has to change the row count.  See the contract at
// the top of TableModel.hpp.
void TreeTableModel::tableToggleExpansion(int row) {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode) return;
  Node& n = nodes_[std::size_t(id)];
  // Already waiting.  A second click is not a second request -- an operator
  // drumming on a slow branch would otherwise queue five fetches for it.
  if (n.state == LoadState::Loading) return;

  // A branch nobody has fetched yet: move to Loading, rebuild so the spinner is
  // already on screen, and ASK.
  const bool fetch =
      (n.state == LoadState::Pending || n.state == LoadState::Failed);
  if (fetch) {
    n.state = LoadState::Loading;
  } else {
    if (n.children.empty()) return;
    n.expanded = !n.expanded;
  }
  rebuildVisible();

  // THE ASK IS THE LAST STATEMENT, and the shape of this function was chosen to
  // make it so.  The slot that answers may legitimately call finishLoad
  // synchronously -- which re-enters this object and rebuilds the row list under
  // this frame -- so there must be nothing left here to corrupt when it does.
  if (!fetch) return;
  childrenRequested.emit(id);
}

bool TreeTableModel::tableSetText(int row, int col, const std::string& value) {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode || col < 0) return false;
  Node& n = nodes_[std::size_t(id)];
  if (int(n.cells.size()) <= col) n.cells.resize(std::size_t(col) + 1);
  n.cells[std::size_t(col)] = value;
  return true;
}

bool TreeTableModel::tableSetFlag(int row, int col, bool on) {
  const NodeId id = nodeAtRow(row);
  if (id == kInvalidNode || col < 0) return false;
  setFlag(id, col, on);
  return true;
}

}  // namespace geeyoou
