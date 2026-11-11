// Header field line rules: HF-01 to HF-06 and G-01, G-02.
#include <string>

#include "test.hpp"

using framecheck::ErrorClass;

namespace {
const std::string kLine = "GET / HTTP/1.1\r\n";
}

TEST(whitespace_before_colon_rejected) {
  std::string s = kLine + "Host : example\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::WsBeforeColon, s.find(' ', kLine.size()));
}

TEST(tab_before_colon_rejected) {
  std::string s = kLine + "Host\t: example\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::WsBeforeColon, s.find('\t'));
}

TEST(obs_fold_rejected) {
  // HF-03: a continuation line is rejected at its first byte.
  std::string s = kLine + "A: b\r\n c\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::ObsFold, s.find(" c"));
}

TEST(whitespace_led_first_field_rejected) {
  std::string s = kLine + " X: y\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::ObsFold, kLine.size());
}

TEST(header_name_with_invalid_byte_rejected) {
  std::string s = kLine + "Bad/Name: x\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadFieldName, s.find('/', kLine.size()));
}

TEST(empty_header_name_rejected) {
  std::string s = kLine + ": x\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadFieldName, kLine.size());
}

TEST(line_without_colon_rejected) {
  std::string s = kLine + "NoColon\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadFieldName, s.find('\r', kLine.size()));
}

TEST(obs_text_in_field_value_rejected) {
  std::string s = kLine + "X: \xe9\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadFieldValue, s.find('\xe9'));
}

TEST(del_byte_in_field_value_rejected) {
  std::string s = kLine + std::string("X: a\x7f") + "\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadFieldValue, s.find('\x7f'));
}

TEST(empty_field_value_accepted) {
  // HF-05. "X:" with nothing after it is a valid field line.
  std::string s = kLine + "X:\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(bare_lf_in_header_rejected) {
  std::string s = kLine + "X: y\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BareLf, s.find('\n', kLine.size()));
}

TEST(bare_cr_in_header_value_rejected) {
  std::string s = kLine + "X: a\rb\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BareCr, s.find('\r', kLine.size()));
}

TEST(content_length_value_is_trimmed_of_ows) {
  // HF-04: spaces and tabs around the value are not part of it.
  std::string s = "POST / HTTP/1.1\r\nContent-Length: \t 5 \t\r\n\r\nhello";
  EXPECT_COMPLETE(s, s.size(), 5);
}

TEST(header_section_ends_at_empty_line) {
  // HF-06. Header values after the empty line belong to the body.
  std::string head = kLine + "X: y\r\n";
  EXPECT_COMPLETE(head + "\r\nX: z\r\n\r\n", head.size() + 2, 0);
}

TEST(incomplete_header_is_incomplete) {
  EXPECT_INCOMPLETE(kLine + "Host: example");
}

TEST(eager_bad_name_byte_is_error_before_header_end) {
  // The bad byte is reported even though the header section never ends.
  std::string s = kLine + "Bad/";
  EXPECT_ERROR(s, ErrorClass::BadFieldName, s.find('/', kLine.size()));
}

TEST(tchar_punctuation_in_field_name_accepted) {
  // HF-02 and RFC 9110 section 5.6.2: every tchar is a valid name byte. The
  // name below uses the ones that are easy to drop by accident, such as '*'.
  std::string s = kLine + "X*Y!#$%&'+-.^_`|~: v\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(bare_lf_ends_header_section_rejected) {
  // HF-06 and G-01. A bare LF where the empty line should be is not the end of
  // the header section. It is a bare LF, and the LF is the last byte here.
  std::string s = kLine + "X: y\r\n\n";
  EXPECT_ERROR(s, ErrorClass::BareLf, s.size() - 1);
}
