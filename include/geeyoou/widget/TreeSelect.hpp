#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/SelectBase.hpp"
#include "geeyoou/widget/SelectModel.hpp"

namespace geeyoou {

// Hierarchical dropdown -- 装置 → 单元 → 位号.
//
// The tree is flattened to a row list on every open/expand, and only the rows
// currently on screen are drawn.  Flattening is cheap (one walk) and keeps
// PopupList free of any tree awareness.
class TreeSelect : public SelectBase {
 public:
  GEEYOOU_STYLE_TYPE(TreeSelect, SelectBase)

  void setRoots(std::vector<TreeItem> roots);
  const std::vector<TreeItem>& roots() const { return roots_; }

  // When true (the default) only leaves can be chosen, and activating a branch
  // expands it instead.  An equipment tree usually wants this; a category
  // picker usually does not.
  void setLeafOnly(bool on);
  bool leafOnly() const { return leafOnly_; }

  // Shows the full path ("反应单元 / R-101 / 温度") in the closed field.
  void setShowPath(bool on);

  void expandAll();
  void collapseAll();

  const std::string& selectedValue() const { return selectedValue_; }
  const std::string& selectedText() const { return selectedText_; }
  void setSelectedValue(const std::string& value);

  Signal<const std::string&> selectionChanged;  // value

 protected:
  std::string displayText() const override;
  bool hasValue() const override { return !selectedText_.empty(); }
  std::vector<PopupRow> buildRows() override;
  void onRowActivated(int row) override;
  void onExpanderToggled(int row) override;

 private:
  // Flattened rows carry a path into the tree rather than a pointer, so the
  // vector<TreeItem> is free to reallocate between builds.
  struct FlatRef {
    std::vector<int> path;
    bool leaf = false;
  };

  TreeItem* nodeAt(const std::vector<int>& path);
  void flatten(std::vector<TreeItem>& nodes, std::vector<int>& path, int depth,
               std::vector<PopupRow>& rows);
  void setExpandedAll(std::vector<TreeItem>& nodes, bool on);
  bool findValue(std::vector<TreeItem>& nodes, const std::string& value,
                 std::vector<std::string>& trail);

  std::vector<TreeItem> roots_;
  std::vector<FlatRef> flat_;  // parallel to the rows handed to PopupList
  std::string selectedValue_;
  std::string selectedText_;
  bool leafOnly_ = true;
  bool showPath_ = true;
};

}  // namespace geeyoou
