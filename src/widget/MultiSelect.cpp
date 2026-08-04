#include "geeyoou/widget/MultiSelect.hpp"

#include <algorithm>

namespace geeyoou {

void MultiSelect::setItems(std::vector<SelectItem> items) {
  items_ = std::move(items);
  checked_.assign(items_.size(), false);
  if (isOpen()) refreshRows();
  update();
}

bool MultiSelect::isChecked(int index) const {
  if (index < 0 || index >= int(checked_.size())) return false;
  return checked_[std::size_t(index)];
}

void MultiSelect::setChecked(int index, bool on) {
  if (index < 0 || index >= int(items_.size())) return;
  const SelectItem& it = items_[std::size_t(index)];
  if (it.header || !it.enabled) return;
  if (checked_[std::size_t(index)] == on) return;
  checked_[std::size_t(index)] = on;
  if (isOpen()) refreshRows();
  update();
  selectionChanged.emit();
}

void MultiSelect::toggle(int index) { setChecked(index, !isChecked(index)); }

void MultiSelect::checkAll() {
  bool any = false;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].header || !items_[i].enabled) continue;
    if (!checked_[i]) { checked_[i] = true; any = true; }
  }
  if (!any) return;
  if (isOpen()) refreshRows();
  update();
  selectionChanged.emit();
}

void MultiSelect::clearAll() {
  bool any = false;
  for (std::size_t i = 0; i < checked_.size(); ++i) {
    if (checked_[i]) { checked_[i] = false; any = true; }
  }
  if (!any) return;
  if (isOpen()) refreshRows();
  update();
  selectionChanged.emit();
}

std::vector<int> MultiSelect::checkedIndices() const {
  std::vector<int> out;
  for (std::size_t i = 0; i < checked_.size(); ++i) {
    if (checked_[i]) out.push_back(int(i));
  }
  return out;
}

std::vector<std::string> MultiSelect::checkedValues() const {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < checked_.size(); ++i) {
    if (!checked_[i]) continue;
    const SelectItem& it = items_[i];
    out.push_back(it.value.empty() ? it.text : it.value);
  }
  return out;
}

int MultiSelect::checkedCount() const {
  return int(std::count(checked_.begin(), checked_.end(), true));
}

void MultiSelect::setSummaryThreshold(int n) {
  summaryThreshold_ = std::max(0, n);
  update();
}

void MultiSelect::setSelectAllEnabled(bool on) {
  selectAll_ = on;
  if (isOpen()) refreshRows();
}

std::string MultiSelect::displayText() const {
  const int n = checkedCount();
  if (n == 0) return {};
  if (n > summaryThreshold_) return std::to_string(n) + " 项已选";
  std::string out;
  for (std::size_t i = 0; i < checked_.size(); ++i) {
    if (!checked_[i]) continue;
    if (!out.empty()) out += "、";
    out += items_[i].text;
  }
  return out;
}

std::vector<PopupRow> MultiSelect::buildRows() {
  std::vector<PopupRow> rows;
  rows.reserve(items_.size() + 1);

  if (selectAll_ && !items_.empty()) {
    const int n = checkedCount();
    PopupRow r;
    r.text = (n > 0) ? "清空全部" : "全选";
    r.secondary = std::to_string(n) + "/" + std::to_string(items_.size());
    r.modelIndex = kSelectAllRow;
    r.icon = (n > 0) ? Icon::Close : Icon::Check;
    rows.push_back(std::move(r));
  }

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const SelectItem& it = items_[i];
    PopupRow r;
    r.text = it.text;
    r.secondary = it.secondary;
    r.modelIndex = int(i);
    r.header = it.header;
    r.enabled = it.enabled;
    r.icon = it.icon;
    if (!it.header) {
      r.checkable = true;
      r.checked = checked_[i];
    }
    rows.push_back(std::move(r));
  }
  return rows;
}

void MultiSelect::onRowActivated(int row) {
  const auto& rows = list()->rows();
  if (row < 0 || row >= int(rows.size())) return;
  const int mi = rows[std::size_t(row)].modelIndex;

  if (mi == kSelectAllRow) {
    // One row that flips meaning: "全选" while anything is unchecked, "清空"
    // once something is. Two rows would waste a line in a nine-row popup.
    if (checkedCount() > 0) clearAll(); else checkAll();
    list()->setHighlighted(row, false);
    return;
  }
  // Clicking anywhere on the row toggles it -- demanding a hit on the 15px
  // checkbox is a poor target on a touch panel.
  toggle(mi);
  list()->setHighlighted(row, false);
}

void MultiSelect::onRowToggled(int row) { onRowActivated(row); }

}  // namespace geeyoou
