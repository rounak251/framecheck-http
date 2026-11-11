// Byte classes from RFC 9110 and RFC 9112, plus the two control-flow signals
// the parser uses internally.
#pragma once

#include <cstddef>
#include <cstdint>

#include "framecheck/parser.hpp"

namespace framecheck::detail {

// The input ended before the message did. parse() turns this into Incomplete.
// Thrown instead of returned so that the parsing code reads straight through.
struct NeedMore {};

// A rule was broken at this offset. parse() turns this into Error.
struct Reject {
  ErrorClass cls;
  size_t offset;
};

// Content-Length values and chunk sizes must not pass 2^63 - 1. The spec table
// makes this cap a choice. RFC 9112 section 7.1 only asks for overflow checks.
constexpr uint64_t kMaxSize = 0x7FFFFFFFFFFFFFFFull;

constexpr uint8_t kCr = '\r';
constexpr uint8_t kLf = '\n';
constexpr uint8_t kSp = ' ';
constexpr uint8_t kHtab = '\t';

// RFC 9110 section 5.6.2: tchar.
inline bool is_tchar(uint8_t c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
    return true;
  }
  switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
      return true;
    default:
      return false;
  }
}

// RFC 9110 section 5.6.3: OWS and BWS are both SP or HTAB.
inline bool is_ows(uint8_t c) { return c == kSp || c == kHtab; }

inline bool is_digit(uint8_t c) { return c >= '0' && c <= '9'; }

inline bool is_hexdig(uint8_t c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline uint64_t hex_value(uint8_t c) {
  if (is_digit(c)) return static_cast<uint64_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint64_t>(c - 'a' + 10);
  return static_cast<uint64_t>(c - 'A' + 10);
}

// Visible US-ASCII, 0x21 to 0x7E (VCHAR in RFC 5234).
inline bool is_vchar(uint8_t c) { return c >= 0x21 && c <= 0x7E; }

// Compares a byte range with a lowercase ASCII literal, ignoring case.
inline bool equals_ci(const uint8_t* begin, const uint8_t* end, const char* lower) {
  const uint8_t* p = begin;
  const char* q = lower;
  while (p != end && *q != '\0') {
    uint8_t c = *p;
    if (c >= 'A' && c <= 'Z') c = static_cast<uint8_t>(c - 'A' + 'a');
    if (c != static_cast<uint8_t>(*q)) return false;
    ++p;
    ++q;
  }
  return p == end && *q == '\0';
}

}  // namespace framecheck::detail
