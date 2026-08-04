#pragma once
//
// UTF-8 navigation.
//
// Text is stored as UTF-8 std::string throughout GeeyoouUI, and a caret is a
// BYTE offset into it.  Every caret movement must land on a codepoint boundary:
// backspacing one byte off a Chinese character would leave a truncated
// sequence that renders as garbage and can never be repaired by further edits.
// These helpers are the only sanctioned way to move a caret.
//
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace geeyoou::utf8 {

inline bool isContinuation(char c) {
  return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

// Byte length of the sequence starting at `i`, or 1 for malformed input so that
// callers always make forward progress instead of looping forever.
inline std::size_t sequenceLength(std::string_view s, std::size_t i) {
  if (i >= s.size()) return 0;
  const auto c = static_cast<unsigned char>(s[i]);
  if (c < 0x80u) return 1;
  if ((c & 0xE0u) == 0xC0u) return 2;
  if ((c & 0xF0u) == 0xE0u) return 3;
  if ((c & 0xF8u) == 0xF0u) return 4;
  return 1;
}

// First boundary strictly after `i`.
inline std::size_t nextBoundary(std::string_view s, std::size_t i) {
  if (i >= s.size()) return s.size();
  std::size_t j = i + sequenceLength(s, i);
  while (j < s.size() && isContinuation(s[j])) ++j;
  return j > s.size() ? s.size() : j;
}

// Last boundary strictly before `i`.
inline std::size_t prevBoundary(std::string_view s, std::size_t i) {
  if (i == 0) return 0;
  std::size_t j = i - 1;
  while (j > 0 && isContinuation(s[j])) --j;
  return j;
}

inline std::size_t codepointCount(std::string_view s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size(); i = nextBoundary(s, i)) ++n;
  return n;
}

// Appends `cp` encoded as UTF-8.  Invalid codepoints are dropped rather than
// written as a replacement character -- a silent no-op is safer in a field the
// operator is about to submit as a setpoint or a recipe name.
inline void append(std::string& out, char32_t cp) {
  if (cp < 0x80u) {
    out += char(cp);
  } else if (cp < 0x800u) {
    out += char(0xC0u | (cp >> 6));
    out += char(0x80u | (cp & 0x3Fu));
  } else if (cp < 0x10000u) {
    if (cp >= 0xD800u && cp <= 0xDFFFu) return;  // lone surrogate
    out += char(0xE0u | (cp >> 12));
    out += char(0x80u | ((cp >> 6) & 0x3Fu));
    out += char(0x80u | (cp & 0x3Fu));
  } else if (cp <= 0x10FFFFu) {
    out += char(0xF0u | (cp >> 18));
    out += char(0x80u | ((cp >> 12) & 0x3Fu));
    out += char(0x80u | ((cp >> 6) & 0x3Fu));
    out += char(0x80u | (cp & 0x3Fu));
  }
}

// Snaps an arbitrary byte index onto the nearest boundary at or before it.
inline std::size_t clampToBoundary(std::string_view s, std::size_t i) {
  if (i >= s.size()) return s.size();
  while (i > 0 && isContinuation(s[i])) --i;
  return i;
}

}  // namespace geeyoou::utf8
