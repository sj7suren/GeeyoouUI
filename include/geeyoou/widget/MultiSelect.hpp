#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/SelectBase.hpp"
#include "geeyoou/widget/SelectModel.hpp"

namespace geeyoou {

// Checkbox dropdown.
//
// Unlike the single-select variants this one does NOT close on activation --
// picking three items should take three clicks, not three reopenings.  It
// closes on Esc, Tab, or a click outside.
class MultiSelect : public SelectBase {
 public:
  GEEYOOU_STYLE_TYPE(MultiSelect, SelectBase)

  void setItems(std::vector<SelectItem> items);
  const std::vector<SelectItem>& items() const { return items_; }

  void setChecked(int index, bool on);
  bool isChecked(int index) const;
  void toggle(int index);
  void checkAll();
  void clearAll();

  std::vector<int> checkedIndices() const;
  std::vector<std::string> checkedValues() const;
  int checkedCount() const;

  // Above this many, the closed field shows "N 项已选" instead of the joined
  // labels.  A field crammed with eight tag names is unreadable.
  void setSummaryThreshold(int n);

  // Adds "全选 / 清空" as the first row.
  void setSelectAllEnabled(bool on);

  Signal<> selectionChanged;

 protected:
  std::string displayText() const override;
  bool hasValue() const override { return checkedCount() > 0; }
  std::vector<PopupRow> buildRows() override;
  void onRowActivated(int row) override;
  void onRowToggled(int row) override;
  bool closeOnActivate() const override { return false; }

 private:
  // Sentinel modelIndex for the "全选 / 清空" row.
  static constexpr int kSelectAllRow = -2;

  std::vector<SelectItem> items_;
  std::vector<bool> checked_;
  int summaryThreshold_ = 2;
  bool selectAll_ = true;
};

}  // namespace geeyoou
