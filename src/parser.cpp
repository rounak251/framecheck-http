// Request line, header fields and framing decisions. The rules are the ones
// in docs/spec-table.md. The row IDs are in the comments so that a reader can
// find the matching row.
#include <algorithm>
#include <cstring>

#include "parser_internal.hpp"

namespace framecheck {

const char* error_class_name(ErrorClass cls) {
  switch (cls) {
    case ErrorClass::BareLf: return "bare-lf";
    case ErrorClass::BareCr: return "bare-cr";
    case ErrorClass::BadRequestLine: return "bad-request-line";
    case ErrorClass::WsBeforeColon: return "ws-before-colon";
    case ErrorClass::BadFieldName: return "bad-field-name";
    case ErrorClass::ObsFold: return "obs-fold";
    case ErrorClass::BadFieldValue: return "bad-field-value";
    case ErrorClass::ClTeConflict: return "cl-te-conflict";
    case ErrorClass::DuplicateContentLength: return "duplicate-content-length";
    case ErrorClass::BadContentLength: return "bad-content-length";
    case ErrorClass::BadTransferEncoding: return "bad-transfer-encoding";
    case ErrorClass::BadChunkSize: return "bad-chunk-size";
    case ErrorClass::ChunkSizeOverflow: return "chunk-size-overflow";
    case ErrorClass::BadChunkExt: return "bad-chunk-ext";
    case ErrorClass::BadChunkDataTerminator: return "bad-chunk-data-terminator";
    case ErrorClass::BadTrailer: return "bad-trailer";
  }
  return "unknown";
}

Result parse(const uint8_t* data, size_t size) {
  return detail::Parser(data, size).run();
}

namespace detail {
namespace {

// True when the field name is the lowercase literal. Field names are
// case-insensitive (RFC 9110 section 5.1).
bool name_is(const uint8_t* data, const FieldLine& f, const char* lower) {
  return equals_ci(data + f.name_begin, data + f.name_end, lower);
}

bool all_digits(const uint8_t* begin, const uint8_t* end) {
  if (begin == end) return false;
  for (const uint8_t* p = begin; p != end; ++p) {
    if (!is_digit(*p)) return false;
  }
  return true;
}

// Two Content-Length values are the same when they are the same decimal
// number, so "5" and "05" match. Anything that is not all digits is compared
// byte for byte. The framing check reports the bad value later.
bool same_value(const uint8_t* data, const FieldLine& a, const FieldLine& b) {
  const uint8_t* a_begin = data + a.value_begin;
  const uint8_t* a_end = data + a.value_end;
  const uint8_t* b_begin = data + b.value_begin;
  const uint8_t* b_end = data + b.value_end;
  if (!all_digits(a_begin, a_end) || !all_digits(b_begin, b_end)) {
    return std::equal(a_begin, a_end, b_begin, b_end);
  }
  while (a_begin != a_end && *a_begin == '0') ++a_begin;
  while (b_begin != b_end && *b_begin == '0') ++b_begin;
  return std::equal(a_begin, a_end, b_begin, b_end);
}

// RFC 9110 section 8.6: Content-Length = 1*DIGIT. No sign, no spaces, no
// comma list. The value must not pass kMaxSize (spec table, Conventions).
bool parse_content_length(const uint8_t* data, const FieldLine& f, uint64_t& out) {
  if (!all_digits(data + f.value_begin, data + f.value_end)) return false;
  uint64_t value = 0;
  for (size_t i = f.value_begin; i < f.value_end; ++i) {
    uint64_t d = static_cast<uint64_t>(data[i] - '0');
    if (value > (kMaxSize - d) / 10) return false;
    value = value * 10 + d;
  }
  out = value;
  return true;
}

}  // namespace

Result Parser::run() {
  Result out;
  try {
    parse_request_line();
    parse_header_section();
    check_framing();
    uint64_t body_length = 0;
    if (chunked_) {
      parse_body_chunked();
      body_length = chunked_total_;
    } else if (cl_count_ > 0) {
      // FR-11: the body is exactly the Content-Length octets.
      if (cl_value_ > size_ - pos_) throw NeedMore{};
      pos_ += static_cast<size_t>(cl_value_);
      body_length = cl_value_;
    }
    // FR-10: with no framing field, the body is empty and pos_ is the header end.
    out.kind = Result::Kind::Complete;
    out.consumed = pos_;
    out.body_length = body_length;
  } catch (const NeedMore&) {
    out.kind = Result::Kind::Incomplete;
  } catch (const Reject& r) {
    out.kind = Result::Kind::Error;
    out.error_class = r.cls;
    out.error_offset = r.offset;
  }
  return out;
}

uint8_t Parser::at(size_t i) const {
  if (i >= size_) throw NeedMore{};
  return data_[i];
}

// Callers only get here where a CR can never be valid, such as where a space
// is required. So the error is decided now, even if the input ends at this CR.
// Where a CRLF is allowed, the caller waits for the LF before it gets here.
void Parser::unexpected(size_t i, ErrorClass ctx) const {
  uint8_t b = at(i);
  if (b == kLf) throw Reject{ErrorClass::BareLf, i};  // G-01
  if (b == kCr && i + 1 < size_ && data_[i + 1] != kLf) throw Reject{ErrorClass::BareCr, i};  // G-02
  throw Reject{ctx, i};
}

void Parser::expect_crlf(ErrorClass ctx) {
  if (at(pos_) == kCr && at(pos_ + 1) == kLf) {
    pos_ += 2;
    return;
  }
  unexpected(pos_, ctx);
}

ErrorClass Parser::field_class(ErrorClass specific) const {
  return in_trailer_ ? ErrorClass::BadTrailer : specific;
}

// RL-01 to RL-04.
void Parser::parse_request_line() {
  size_t start = pos_;
  while (is_tchar(at(pos_))) ++pos_;
  if (pos_ == start || at(pos_) != kSp) unexpected(pos_, ErrorClass::BadRequestLine);
  ++pos_;

  start = pos_;
  while (is_vchar(at(pos_))) ++pos_;
  if (pos_ == start || at(pos_) != kSp) unexpected(pos_, ErrorClass::BadRequestLine);
  ++pos_;

  static constexpr char kPrefix[] = "HTTP/1.";
  for (size_t k = 0; k + 1 < sizeof(kPrefix); ++k) {
    if (at(pos_) != static_cast<uint8_t>(kPrefix[k])) unexpected(pos_, ErrorClass::BadRequestLine);
    ++pos_;
  }
  uint8_t minor = at(pos_);
  if (minor != '0' && minor != '1') unexpected(pos_, ErrorClass::BadRequestLine);
  http10_ = (minor == '0');
  ++pos_;
  expect_crlf(ErrorClass::BadRequestLine);
}

// One field line, from its first byte through its CRLF. HF-01 to HF-05, and
// CH-08 for trailers. Every check runs on the bytes as they arrive.
FieldLine Parser::parse_field_line() {
  FieldLine f;
  f.line_start = pos_;
  if (is_ows(at(pos_))) throw Reject{field_class(ErrorClass::ObsFold), pos_};  // HF-03

  f.name_begin = pos_;
  while (is_tchar(at(pos_))) ++pos_;
  f.name_end = pos_;
  if (f.name_end == f.name_begin) unexpected(pos_, field_class(ErrorClass::BadFieldName));  // HF-02

  // CH-08: the name is complete here, so the rejection does not wait for the
  // rest of the line.
  if (in_trailer_ && (name_is(data_, f, "content-length") || name_is(data_, f, "transfer-encoding"))) {
    throw Reject{ErrorClass::BadTrailer, f.line_start};
  }

  if (is_ows(at(pos_))) throw Reject{field_class(ErrorClass::WsBeforeColon), pos_};  // HF-01
  if (at(pos_) != ':') unexpected(pos_, field_class(ErrorClass::BadFieldName));  // HF-02
  ++pos_;

  while (is_ows(at(pos_))) ++pos_;
  f.value_begin = pos_;
  f.value_end = pos_;
  for (;;) {  // HF-04
    uint8_t b = at(pos_);
    if (b == kCr && at(pos_ + 1) == kLf) break;
    if (b == kCr || b == kLf) unexpected(pos_, field_class(ErrorClass::BadFieldValue));
    if (!is_ows(b) && !is_vchar(b)) throw Reject{field_class(ErrorClass::BadFieldValue), pos_};
    ++pos_;
    if (!is_ows(b)) f.value_end = pos_;
  }
  pos_ += 2;
  return f;
}

// Header field lines until the empty line that ends the header section (HF-06).
void Parser::parse_header_section() {
  for (;;) {
    if (at(pos_) == kCr && at(pos_ + 1) == kLf) {
      pos_ += 2;
      return;
    }
    record_header(parse_field_line());
  }
}

// Keeps what the framing rules need. The rules run after the header section.
void Parser::record_header(const FieldLine& f) {
  if (name_is(data_, f, "content-length")) {
    if (cl_count_ == 0) {
      first_cl_ = f;
    } else if (!has_cl_mismatch_ && !same_value(data_, first_cl_, f)) {
      has_cl_mismatch_ = true;
      first_cl_mismatch_ = f.line_start;
    }
    ++cl_count_;
  }
  if (name_is(data_, f, "transfer-encoding")) {
    if (te_count_ == 0) {
      first_te_ = f;
    } else if (te_count_ == 1) {
      second_te_line_ = f.line_start;
    }
    ++te_count_;
  }
}

// FR-01 to FR-09, in the order of the spec table.
void Parser::check_framing() {
  if (cl_count_ > 0 && te_count_ > 0) {  // FR-01
    throw Reject{ErrorClass::ClTeConflict, std::max(first_cl_.line_start, first_te_.line_start)};
  }
  if (has_cl_mismatch_) {  // FR-02
    throw Reject{ErrorClass::DuplicateContentLength, first_cl_mismatch_};
  }
  // FR-02 has already made every Content-Length identical, so checking the
  // first one checks them all.
  if (cl_count_ > 0 && !parse_content_length(data_, first_cl_, cl_value_)) {  // FR-03
    throw Reject{ErrorClass::BadContentLength, first_cl_.line_start};
  }
  if (te_count_ > 1) {  // FR-04
    throw Reject{ErrorClass::BadTransferEncoding, second_te_line_};
  }
  if (te_count_ == 1) check_transfer_encoding(first_te_);
}

// FR-05 to FR-07 and FR-09. Every violation here is reported at the line start.
void Parser::check_transfer_encoding(const FieldLine& te) {
  size_t p = te.value_begin;
  size_t chunked_count = 0;
  bool last_is_chunked = false;
  for (;;) {
    while (p < te.value_end && is_ows(data_[p])) ++p;
    size_t start = p;
    while (p < te.value_end && is_tchar(data_[p])) ++p;
    if (p == start) throw Reject{ErrorClass::BadTransferEncoding, te.line_start};  // empty element
    last_is_chunked = equals_ci(data_ + start, data_ + p, "chunked");
    if (last_is_chunked) ++chunked_count;
    while (p < te.value_end && is_ows(data_[p])) ++p;
    if (p == te.value_end) break;
    // A parameter such as ";q=1" lands here too. FR-05 rejects it.
    if (data_[p] != ',') throw Reject{ErrorClass::BadTransferEncoding, te.line_start};
    ++p;
  }
  if (!last_is_chunked) throw Reject{ErrorClass::BadTransferEncoding, te.line_start};  // FR-06
  if (chunked_count > 1) throw Reject{ErrorClass::BadTransferEncoding, te.line_start};  // FR-07
  if (http10_) throw Reject{ErrorClass::BadTransferEncoding, te.line_start};  // FR-09
  chunked_ = true;
}

}  // namespace detail
}  // namespace framecheck
