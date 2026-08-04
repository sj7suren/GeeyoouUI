#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/render/Icon.hpp"

namespace geeyoou {

// One entry in any of the select controls.
//
// The reason for `extraFields` is the multi-field search requirement: an
// operator hunting for a tag may remember its address, its unit, or the Chinese
// description, and should find it by any of them -- while only `text` and
// `secondary` are ever DISPLAYED.  Keeping hidden search keys in the model
// beats forcing the caller to cram everything into the visible label.
struct SelectItem {
  std::string text;       // primary label
  std::string secondary;  // dimmed, right-aligned (unit, address, count...)
  std::string value;      // opaque id handed back to the application
  std::vector<std::string> extraFields;  // searched but never drawn

  Icon icon = Icon::None;
  bool enabled = true;
  bool header = false;  // non-selectable group heading

  SelectItem() = default;
  SelectItem(std::string t) : text(std::move(t)) {}
  SelectItem(std::string t, std::string v) : text(std::move(t)), value(std::move(v)) {}
  SelectItem(std::string t, std::string sec, std::string v)
      : text(std::move(t)), secondary(std::move(sec)), value(std::move(v)) {}

  static SelectItem group(std::string title) {
    SelectItem it(std::move(title));
    it.header = true;
    it.enabled = false;
    return it;
  }
};

// A node in TreeSelect.  Children are held by value: an HMI equipment tree is
// built once at screen-construction time and then only read, so the simplicity
// of value semantics beats the flexibility of a pointer graph.
struct TreeItem {
  std::string text;
  std::string secondary;
  std::string value;
  std::vector<std::string> extraFields;
  Icon icon = Icon::None;
  bool enabled = true;
  bool expanded = false;
  std::vector<TreeItem> children;

  TreeItem() = default;
  TreeItem(std::string t) : text(std::move(t)) {}
  TreeItem(std::string t, std::string v) : text(std::move(t)), value(std::move(v)) {}

  bool isLeaf() const { return children.empty(); }
};

}  // namespace geeyoou
