#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/ComboBox.hpp"

namespace geeyoou {

// Single-select with type-to-filter.
//
// Search spans EVERY field of an item -- `text`, `secondary`, and each entry of
// `extraFields` -- because an operator hunting for a tag may remember its
// address, its unit or its Chinese description, and any of those should find
// it.  Only a match inside `text` gets highlighted, since that is the only one
// on screen.
//
// While open the field becomes a live query box; the previously selected value
// reappears if the popup is dismissed without choosing.
class SearchableSelect : public ComboBox {
 public:
  GEEYOOU_STYLE_TYPE(SearchableSelect, ComboBox)

  const std::string& query() const { return query_; }

  // Restrict the search to the visible label only.  Off by default -- the
  // whole point of this control is the hidden fields.
  void setSearchVisibleTextOnly(bool on);

  // Emitted whenever the filter text changes; useful for server-side lookup.
  Signal<const std::string&> queryChanged;
  // Emitted when the filter matches nothing, so a screen can offer "新建…".
  Signal<const std::string&> noMatch;

 protected:
  std::string displayText() const override;
  std::vector<PopupRow> buildRows() override;
  bool showCaret() const override { return true; }
  bool handleKeyWhileOpen(const KeyEvent& e) override;
  void onOpened() override;
  void onClosed() override;

 private:
  // Case-insensitive for ASCII, exact for anything else (a byte-wise lowercase
  // of UTF-8 would corrupt multibyte sequences, and CJK has no case anyway).
  static bool fieldMatches(const std::string& field, const std::string& lowerQuery,
                           std::size_t* matchStart);
  void setQuery(std::string q);

  std::string query_;
  bool visibleOnly_ = false;
};

}  // namespace geeyoou
