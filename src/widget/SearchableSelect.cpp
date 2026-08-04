#include "geeyoou/widget/SearchableSelect.hpp"

#include <algorithm>
#include <cctype>

#include "geeyoou/core/Utf8.hpp"

namespace geeyoou {
namespace {

// Lowercases ASCII bytes only.  Bytes >= 0x80 are left untouched, which keeps
// UTF-8 sequences intact -- a naive ::tolower over every byte would corrupt
// Chinese text into garbage that then fails to match anything.
std::string asciiLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x80) c = char(std::tolower(u));
  }
  return out;
}

}  // namespace

void SearchableSelect::setSearchVisibleTextOnly(bool on) {
  visibleOnly_ = on;
  if (isOpen()) refreshRows();
}

bool SearchableSelect::fieldMatches(const std::string& field,
                                    const std::string& lowerQuery,
                                    std::size_t* matchStart) {
  if (lowerQuery.empty()) return true;
  const std::string lf = asciiLower(field);
  const std::size_t pos = lf.find(lowerQuery);
  if (pos == std::string::npos) return false;
  // asciiLower preserves byte length, so an offset found in the lowered copy
  // is valid in the original -- that is what makes the highlight land right.
  if (matchStart) *matchStart = pos;
  return true;
}

void SearchableSelect::setQuery(std::string q) {
  if (query_ == q) return;
  query_ = std::move(q);
  refreshRows();
  list()->highlightFirstSelectable();
  update();
  queryChanged.emit(query_);
  if (list()->empty()) noMatch.emit(query_);
}

std::string SearchableSelect::displayText() const {
  // While open the field IS the query box; closed, it shows the selection.
  if (isOpen()) return query_;
  return ComboBox::displayText();
}

std::vector<PopupRow> SearchableSelect::buildRows() {
  const std::string q = asciiLower(query_);
  std::vector<PopupRow> rows;
  rows.reserve(items_.size());

  int shortcut = 0;
  // Group headers are only emitted when at least one member survived the
  // filter, otherwise a search leaves a page of empty section titles.
  std::size_t pendingHeader = std::string::npos;

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const SelectItem& it = items_[i];
    if (it.header) {
      pendingHeader = i;
      continue;
    }

    std::size_t mStart = 0;
    bool hit = fieldMatches(it.text, q, &mStart);
    std::size_t highlightStart = hit ? mStart : 0;
    std::size_t highlightLen = hit && !q.empty() ? q.size() : 0;

    if (!hit && !visibleOnly_) {
      // Hidden-field hits still select the row; there is simply nothing on
      // screen to underline.
      hit = fieldMatches(it.secondary, q, nullptr);
      for (const std::string& f : it.extraFields) {
        if (hit) break;
        hit = fieldMatches(f, q, nullptr);
      }
    }
    if (!hit) continue;

    if (pendingHeader != std::string::npos) {
      PopupRow h;
      h.text = items_[pendingHeader].text;
      h.modelIndex = int(pendingHeader);
      h.header = true;
      h.enabled = false;
      rows.push_back(std::move(h));
      pendingHeader = std::string::npos;
    }

    PopupRow r;
    r.text = it.text;
    r.secondary = it.secondary;
    r.modelIndex = int(i);
    r.enabled = it.enabled;
    r.selected = (int(i) == current_);
    r.icon = it.icon;
    r.matchStart = highlightStart;
    r.matchLen = highlightLen;
    if (shortcuts_ && it.enabled && shortcut < 9) r.shortcut = ++shortcut;
    rows.push_back(std::move(r));
  }
  return rows;
}

bool SearchableSelect::handleKeyWhileOpen(const KeyEvent& e) {
  if (e.key == Key::Backspace) {
    if (!query_.empty()) {
      // Whole codepoint, never one byte -- otherwise backspacing a Chinese
      // query character leaves a broken UTF-8 tail that matches nothing.
      setQuery(query_.substr(0, utf8::prevBoundary(query_, query_.size())));
    }
    return true;
  }
  if (e.character != 0 && !e.ctrl && !e.alt) {
    std::string q = query_;
    utf8::append(q, char32_t(e.character));
    setQuery(std::move(q));
    return true;
  }
  return false;
}

void SearchableSelect::onOpened() {
  // Start from a clean query: an operator opening the list wants to search, not
  // to resume editing whatever they typed last time.
  query_.clear();
  refreshRows();
  ComboBox::onOpened();
}

void SearchableSelect::onClosed() {
  query_.clear();
  update();
}

}  // namespace geeyoou
