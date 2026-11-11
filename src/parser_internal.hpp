// The parser state. parser.cpp holds the request line, header fields and
// framing rules. chunked.cpp holds the chunked body and trailers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "bytes.hpp"
#include "framecheck/parser.hpp"

namespace framecheck::detail {

// One header or trailer field line. The value bounds exclude the OWS at both
// ends (HF-04), so the framing code can compare values directly.
struct FieldLine {
  size_t line_start = 0;
  size_t name_begin = 0;
  size_t name_end = 0;
  size_t value_begin = 0;
  size_t value_end = 0;
};

class Parser {
 public:
  Parser(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  Result run();

 private:
  // Byte access. Throws NeedMore when the index is past the input.
  uint8_t at(size_t i) const;

  // Called when the byte at i cannot continue the current rule. A bare LF or a
  // CR without LF is reported as bare-lf or bare-cr (G-01, G-02). Anything
  // else is reported as ctx.
  [[noreturn]] void unexpected(size_t i, ErrorClass ctx) const;

  // Consumes CRLF at pos_, or reports the problem with ctx.
  void expect_crlf(ErrorClass ctx);

  // Field lines in a header section are reported with their own classes.
  // Field lines in a trailer are always bad-trailer (CH-07).
  ErrorClass field_class(ErrorClass specific) const;

  void parse_request_line();
  FieldLine parse_field_line();
  void parse_header_section();
  void record_header(const FieldLine& field);
  void check_framing();
  void check_transfer_encoding(const FieldLine& field);
  void parse_body_chunked();   // chunked.cpp
  void parse_trailer_section();  // chunked.cpp
  uint64_t parse_chunk_size_line();  // chunked.cpp
  void parse_chunk_exts();  // chunked.cpp
  void parse_chunk_ext_value();  // chunked.cpp
  void parse_quoted_string();  // chunked.cpp

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;

  bool in_trailer_ = false;
  bool http10_ = false;

  size_t cl_count_ = 0;
  FieldLine first_cl_;
  size_t first_cl_mismatch_ = 0;  // line start of the first CL that differs
  bool has_cl_mismatch_ = false;
  uint64_t cl_value_ = 0;

  size_t te_count_ = 0;
  FieldLine first_te_;
  size_t second_te_line_ = 0;

  bool chunked_ = false;
  uint64_t chunked_total_ = 0;
};

}  // namespace framecheck::detail
