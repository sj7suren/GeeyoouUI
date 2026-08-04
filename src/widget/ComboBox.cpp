#include "geeyoou/widget/ComboBox.hpp"

#include <algorithm>

namespace geeyoou {

void ComboBox::setItems(std::vector<SelectItem> items) {
  items_ = std::move(items);
  current_ = -1;
  if (isOpen()) refreshRows();
  update();
}

void ComboBox::addItem(SelectItem item) {
  items_.push_back(std::move(item));
  if (isOpen()) refreshRows();
  update();
}

void ComboBox::clearItems() {
  items_.clear();
  current_ = -1;
  if (isOpen()) refreshRows();
  update();
}

void ComboBox::setCurrentIndex(int index) {
  if (index < -1 || index >= int(items_.size())) return;
  if (index >= 0 && (items_[std::size_t(index)].header ||
                     !items_[std::size_t(index)].enabled)) {
    return;  // headers and disabled entries are not selectable values
  }
  if (current_ == index) return;
  current_ = index;
  update();
  currentIndexChanged.emit(current_);
  currentValueChanged.emit(currentValue());
}

void ComboBox::setCurrentValue(const std::string& value) {
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (!items_[i].header && items_[i].value == value) {
      setCurrentIndex(int(i));
      return;
    }
  }
}

std::string ComboBox::currentValue() const {
  const SelectItem* it = currentItem();
  if (!it) return {};
  return it->value.empty() ? it->text : it->value;
}

const SelectItem* ComboBox::currentItem() const {
  if (current_ < 0 || current_ >= int(items_.size())) return nullptr;
  return &items_[std::size_t(current_)];
}

void ComboBox::setShortcutsEnabled(bool on) {
  shortcuts_ = on;
  if (isOpen()) refreshRows();
}

std::string ComboBox::displayText() const {
  const SelectItem* it = currentItem();
  return it ? it->text : std::string{};
}

std::vector<PopupRow> ComboBox::buildRows() {
  std::vector<PopupRow> rows;
  rows.reserve(items_.size());
  int shortcut = 0;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const SelectItem& it = items_[i];
    PopupRow r;
    r.text = it.text;
    r.secondary = it.secondary;
    r.modelIndex = int(i);
    r.header = it.header;
    r.enabled = it.enabled;
    r.selected = (int(i) == current_);
    r.icon = it.icon;
    if (shortcuts_ && !it.header && it.enabled && shortcut < 9) {
      r.shortcut = ++shortcut;
    }
    rows.push_back(std::move(r));
  }
  return rows;
}

void ComboBox::onRowActivated(int row) {
  const auto& rows = list()->rows();
  if (row < 0 || row >= int(rows.size())) return;
  setCurrentIndex(rows[std::size_t(row)].modelIndex);
}

void ComboBox::onOpened() {
  // Open onto the current value rather than the top of the list, so reopening
  // a long list does not make the operator hunt for where they were.
  if (current_ < 0) return;
  const auto& rows = list()->rows();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].modelIndex == current_) {
      list()->setHighlighted(int(i));
      return;
    }
  }
}

}  // namespace geeyoou
