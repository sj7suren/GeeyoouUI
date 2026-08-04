#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/PopupList.hpp"
#include "geeyoou/widget/SelectBase.hpp"
#include "geeyoou/widget/SelectModel.hpp"

namespace geeyoou {

// Column-per-level selector: 车间 → 产线 → 设备.
//
// Different from TreeSelect on purpose.  A tree shows the whole hierarchy
// indented in one column and is best when the operator is BROWSING; a cascader
// shows one column per level and is best when they already know the path and
// want to walk it left to right without the other branches in the way.
//
// The popup is a plain container holding one PopupList per level -- reusing the
// list rather than writing a second row renderer.
class Cascader : public SelectBase {
 public:
  GEEYOOU_STYLE_TYPE(Cascader, SelectBase)

  void setRoots(std::vector<TreeItem> roots);

  // Require a leaf before reporting a selection.  Off means any level commits.
  void setLeafOnly(bool on) { leafOnly_ = on; }
  void setColumnWidth(float w);
  void setMaxVisibleRows(int n);

  const std::vector<std::string>& selectedPath() const { return pathTexts_; }
  const std::string& selectedValue() const { return value_; }
  void clearSelection();

  void open() override;
  void close() override;

  Signal<const std::string&> selectionChanged;  // leaf value

 protected:
  std::string displayText() const override;
  bool hasValue() const override { return !value_.empty(); }

 private:
  void rebuildColumns();
  std::vector<TreeItem>* levelNodes(int level);
  void chooseAt(int level, int rowIndex);

  std::vector<TreeItem> roots_;
  // Index chosen in each column; size == number of open columns - 1.
  std::vector<int> picks_;
  std::vector<std::string> pathTexts_;
  std::string value_;

  Widget* popupBox_ = nullptr;
  std::vector<PopupList*> columns_;
  float columnWidth_ = 170.0f;
  int maxRows_ = 8;
  bool leafOnly_ = true;
};

}  // namespace geeyoou
