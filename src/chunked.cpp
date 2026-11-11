// Chunked transfer coding: chunk sizes, chunk extensions, chunk data and the
// trailer section. Rules are from docs/spec-table.md (CH-01 to CH-09).
#include "parser_internal.hpp"

namespace framecheck::detail {

// Chunk after chunk, until the zero-size chunk. CH-05 and CH-09.
void Parser::parse_body_chunked() {
  for (;;) {
    uint64_t size = parse_chunk_size_line();
    if (size == 0) {
      parse_trailer_section();
      return;
    }
    if (size > size_ - pos_) throw NeedMore{};
    pos_ += static_cast<size_t>(size);
    chunked_total_ += size;
    expect_crlf(ErrorClass::BadChunkDataTerminator);
  }
}

// Reads the chunk-size line through its CRLF and returns the size.
uint64_t Parser::parse_chunk_size_line() {
  // CH-01 and CH-02. The overflow check runs on each digit as it arrives.
  uint64_t size = 0;
  size_t start = pos_;
  while (is_hexdig(at(pos_))) {
    uint64_t d = hex_value(data_[pos_]);
    if (size > (kMaxSize - d) / 16) throw Reject{ErrorClass::ChunkSizeOverflow, pos_};
    size = size * 16 + d;
    ++pos_;
  }
  if (pos_ == start) unexpected(pos_, ErrorClass::BadChunkSize);

  // CH-03: whitespace may come before ';', but not directly before the CRLF.
  size_t after_digits = pos_;
  while (is_ows(at(pos_))) ++pos_;
  if (at(pos_) == ';') {
    parse_chunk_exts();
  } else if (pos_ != after_digits) {
    unexpected(pos_, ErrorClass::BadChunkSize);
  }
  expect_crlf(ErrorClass::BadChunkSize);
  return size;
}

// chunk-ext = *( BWS ";" BWS chunk-ext-name [ BWS "=" BWS chunk-ext-val ] )
// Stops with pos_ on the CR that ends the chunk-size line. Unknown extensions
// are accepted (RFC 9112 section 7.1.1).
void Parser::parse_chunk_exts() {
  for (;;) {
    ++pos_;  // the ';'
    while (is_ows(at(pos_))) ++pos_;
    size_t name_start = pos_;
    while (is_tchar(at(pos_))) ++pos_;
    if (pos_ == name_start) unexpected(pos_, ErrorClass::BadChunkExt);

    // The whitespace before a ';' or a CR is only part of the grammar when a
    // value follows the name, so we back up if there is no '='.
    size_t after_name = pos_;
    while (is_ows(at(pos_))) ++pos_;
    if (at(pos_) == '=') {
      ++pos_;
      while (is_ows(at(pos_))) ++pos_;
      parse_chunk_ext_value();
    } else {
      pos_ = after_name;
    }

    size_t before_ws = pos_;
    while (is_ows(at(pos_))) ++pos_;
    if (at(pos_) == ';') continue;
    // Whitespace before the CRLF is not in the grammar. Report it as a bad
    // extension, which the spec table allows for a malformed extension.
    if (pos_ != before_ws) unexpected(pos_, ErrorClass::BadChunkExt);
    if (at(pos_) != kCr) unexpected(pos_, ErrorClass::BadChunkExt);
    return;
  }
}

// chunk-ext-val = token / quoted-string
void Parser::parse_chunk_ext_value() {
  if (at(pos_) == '"') {
    parse_quoted_string();
    return;
  }
  size_t start = pos_;
  while (is_tchar(at(pos_))) ++pos_;
  if (pos_ == start) unexpected(pos_, ErrorClass::BadChunkExt);
}

// RFC 9110 section 5.6.4, narrowed as CH-03 says: no obs-text, and a
// quoted-pair may only escape HTAB, SP or VCHAR.
void Parser::parse_quoted_string() {
  ++pos_;  // opening DQUOTE
  for (;;) {
    uint8_t b = at(pos_);
    if (b == '"') {
      ++pos_;
      return;
    }
    if (b == '\\') {
      uint8_t escaped = at(pos_ + 1);
      if (!is_ows(escaped) && !is_vchar(escaped)) unexpected(pos_ + 1, ErrorClass::BadChunkExt);
      pos_ += 2;
    } else if (is_ows(b) || b == 0x21 || (b >= 0x23 && b <= 0x5B) || (b >= 0x5D && b <= 0x7E)) {
      ++pos_;  // qdtext
    } else {
      unexpected(pos_, ErrorClass::BadChunkExt);
    }
  }
}

// CH-07 and CH-08. Field lines until the empty line. Each line goes through
// parse_field_line with in_trailer_ set, so every error here is bad-trailer.
void Parser::parse_trailer_section() {
  in_trailer_ = true;
  for (;;) {
    if (at(pos_) == kCr && at(pos_ + 1) == kLf) {
      pos_ += 2;
      break;
    }
    parse_field_line();
  }
  in_trailer_ = false;
}

}  // namespace framecheck::detail
