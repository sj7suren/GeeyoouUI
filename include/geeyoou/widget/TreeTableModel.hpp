#pragma once
//
// A tree of rows, flattened into the numbered rows a TableView draws.
//
// -----------------------------------------------------------------------------
// WHY THIS ONE OWNS ITS DATA, when the whole library is built on not copying.
//
// The rule (architecture section 1, rule 2) is that the WIDGET must not copy the
// application's data -- and it does not: TableView still holds a count and pulls
// per cell.  A tree is different in kind from a list, though.  Nobody has a
// 200 000-node equipment tree already sitting in memory in flattened,
// expansion-aware order; the flattening IS the structure, and something has to
// own it.  Better here, in a plain non-visual object the application can hold,
// than smuggled into the view where it would be invisible and untestable.
//
// -----------------------------------------------------------------------------
// ASYNCHRONOUS CHILDREN -- THE SHAPE, AND WHY IT IS THIS SHAPE
//
// This model NEVER fetches anything.  It emits `childrenRequested` and waits.
//
//     tree.setLazy(node, true);              // "there is more under here"
//     tree.childrenRequested.connect([&](TreeTableModel::NodeId id) {
//       pending.push_back(id);               // remember; do NOT block
//     });
//     ... on the UI thread, when the answer arrives:
//     tree.addNode(id, {"PT-1041", "压力变送器", "在线"});
//     tree.finishLoad(id, true);             // marks Loaded and expands
//
// What is deliberately NOT offered is a `std::function<void(NodeId, Callback)>`
// fetcher.  That shape READS as though the callback may be invoked from whatever
// thread the answer arrives on, and the first person to do so has written a
// worker thread that touches the widget tree -- the one thing
// docs/architecture.md section 3.11 forbids outright, and the only failure in
// this library that cannot be made safe after the fact.  A signal that hands
// back an id and nothing else cannot be misread that way: there is no callback
// to call from the wrong thread.
//
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/widget/TableModel.hpp"

namespace geeyoou {

class TreeTableModel : public TableModel {
 public:
  using NodeId = int;
  static constexpr NodeId kInvalidNode = -1;
  // The id every root hangs off.  A real node, never drawn, so that "add a root"
  // and "add a child" are the same call with a different argument -- the special
  // case is spent once, here, instead of at every call site.
  static constexpr NodeId kRootNode = 0;

  // Where a node's children stand.  Note that Ready and Failed are BOTH
  // terminal: a failed branch keeps its retry affordance and does not
  // automatically ask again, because an equipment tree pointed at an unreachable
  // station would otherwise re-request on every repaint.
  enum class LoadState : std::uint8_t {
    None,     // children are already here (or there are none)
    Pending,  // lazy, never asked
    Loading,  // asked, waiting
    Ready,
    Failed,
  };

  TreeTableModel();

  // --- building -------------------------------------------------------------
  NodeId addNode(NodeId parent, std::vector<std::string> cells);
  void setNumber(NodeId node, int col, double v);
  void setFlag(NodeId node, int col, bool on);
  void setAccent(NodeId node, Color c);
  // "There is something under here that has not been fetched yet."  The node
  // draws a collapsed expander even though it has no children.
  void setLazy(NodeId node, bool on);
  void clear();

  // --- asynchronous loading -------------------------------------------------
  //
  // finishLoad(id, true) marks the branch Ready and opens it; false marks it
  // Failed, which draws the retry glyph.  Both rebuild the visible row list and
  // then emit structureChanged -- connect that to TableView::rowsReset.
  void finishLoad(NodeId node, bool ok);
  LoadState loadState(NodeId node) const;

  // --- expansion ------------------------------------------------------------
  void setExpanded(NodeId node, bool on);
  bool isExpanded(NodeId node) const;
  void expandAll();
  void collapseAll();

  // --- queries --------------------------------------------------------------
  int nodeCount() const { return int(nodes_.size()); }
  NodeId nodeAtRow(int row) const;
  int rowOfNode(NodeId node) const;
  const std::vector<std::string>& cellsOf(NodeId node) const;
  NodeId parentOf(NodeId node) const;
  const std::vector<NodeId>& childrenOf(NodeId node) const;

  // Emitted when the visible row list changed shape.  The application connects
  // it to its TableView::rowsReset -- the model has no pointer to the view and
  // never will, which is what lets one model feed two tables.
  Signal<> structureChanged;
  // "Somebody opened a lazy branch."  Answer on the UI thread; see the header
  // comment for the thread rule this shape exists to protect.
  Signal<NodeId> childrenRequested;

  // --- TableModel -----------------------------------------------------------
  int tableRowCount() const override { return int(visible_.size()); }
  std::string tableText(int row, int col) const override;
  double tableNumber(int row, int col) const override;
  bool tableFlag(int row, int col) const override;
  Color tableAccent(int row, int col) const override;
  bool tableCellPresent(int row, int col) const override;
  int tableDepth(int row) const override;
  RowExpansion tableExpansion(int row) const override;
  void tableToggleExpansion(int row) override;
  bool tableSetText(int row, int col, const std::string& value) override;
  bool tableSetFlag(int row, int col, bool on) override;

 private:
  struct Node {
    std::vector<std::string> cells;
    std::vector<double> numbers;
    std::vector<char> flags;  // char, not bool: vector<bool> has no addressable
                              // elements and this is written to by index
    Color accent = Color::rgba(0, 0, 0, 0);
    NodeId parent = kInvalidNode;
    std::vector<NodeId> children;
    int depth = 0;
    bool expanded = false;
    LoadState state = LoadState::None;
  };

  bool valid(NodeId n) const { return n >= 0 && n < int(nodes_.size()); }
  void rebuildVisible();
  void appendSubtree(NodeId node);

  std::vector<Node> nodes_;
  std::vector<NodeId> visible_;  // flattened, in draw order
  std::vector<int> rowOf_;       // node -> visible row, or -1
};

}  // namespace geeyoou
