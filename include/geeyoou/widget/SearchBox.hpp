#pragma once
#include <string>

#include "geeyoou/widget/LineEdit.hpp"

namespace geeyoou {

// Search field: magnifier on the left, clear button on the right.
class SearchBox : public LineEdit {
 public:
  GEEYOOU_STYLE_TYPE(SearchBox, LineEdit)

  SearchBox() {
    setLeadingIcon(Icon::Search);
    setClearButtonEnabled(true);
    setPlaceholder("搜索");

    // Committed search (Enter) is kept separate from `textChanged`, which fires
    // on every keystroke.  A filter over a live alarm list wants the former; a
    // type-ahead over a local array wants the latter.  Exposing both lets the
    // call site pick without SearchBox guessing.
    returnPressed.connect([this] { searchRequested.emit(text()); });
  }

  Signal<const std::string&> searchRequested;
};

}  // namespace geeyoou
