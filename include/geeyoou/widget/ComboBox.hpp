#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/SelectBase.hpp"
#include "geeyoou/widget/SelectModel.hpp"

namespace geeyoou {

// Single-selection dropdown over a flat list.
//
// Group headings are just items with `header = true` (see SelectItem::group),
// rather than a separate model tier -- a flat vector with skip-me flags is
// enough for grouping, and the keyboard already steps over unselectable rows.
class ComboBox : public SelectBase {
 public:
  GEEYOOU_STYLE_TYPE(ComboBox, SelectBase)

  void setItems(std::vector<SelectItem> items);
  void addItem(SelectItem item);
  void clearItems();
  const std::vector<SelectItem>& items() const { return items_; }

  int currentIndex() const { return current_; }
  void setCurrentIndex(int index);
  void setCurrentValue(const std::string& value);
  std::string currentValue() const;
  const SelectItem* currentItem() const;

  // Shows Alt+1..9 badges on the first nine selectable rows.
  void setShortcutsEnabled(bool on);

  Signal<int> currentIndexChanged;
  Signal<const std::string&> currentValueChanged;

 protected:
  std::string displayText() const override;
  bool hasValue() const override { return current_ >= 0; }
  std::vector<PopupRow> buildRows() override;
  void onRowActivated(int row) override;
  void onOpened() override;

  std::vector<SelectItem> items_;
  int current_ = -1;
  bool shortcuts_ = true;
};

}  // namespace geeyoou
