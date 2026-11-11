// Request line rules: RL-01 to RL-04, G-01, G-02, G-04 and the eager rule.
#include <string>

#include "test.hpp"

using framecheck::ErrorClass;

TEST(request_line_minimal_accepted) {
  // "GET / HTTP/1.1" is 14 bytes, then CRLF, then the empty line.
  EXPECT_COMPLETE(std::string("GET / HTTP/1.1\r\n\r\n"), 18, 0);
}

TEST(http10_request_line_accepted) { EXPECT_COMPLETE(std::string("GET / HTTP/1.0\r\n\r\n"), 18, 0); }

TEST(version_other_than_1_0_or_1_1_rejected) {
  std::string s = "GET / HTTP/1.2\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, s.find('2'));
}

TEST(version_name_is_case_sensitive) {
  std::string s = "GET / http/1.1\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, s.find('h'));
}

TEST(bare_lf_in_request_line_rejected) {
  std::string s = "GET / HTTP/1.1\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BareLf, s.find('\n'));
}

TEST(bare_cr_in_request_line_rejected) {
  std::string s = "GET / HTTP/1.1\rX\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BareCr, s.find('\r'));
}

TEST(empty_request_line_rejected) {
  // G-04: an empty line before the request line is rejected at offset 0.
  EXPECT_ERROR(std::string("\r\nGET / HTTP/1.1\r\n\r\n"), ErrorClass::BadRequestLine, 0);
}

TEST(double_space_in_request_line_rejected) {
  std::string s = "GET  / HTTP/1.1\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 4);
}

TEST(method_is_a_case_sensitive_token) {
  // "get" is a valid token. The parser does not change its case.
  EXPECT_COMPLETE(std::string("get / HTTP/1.1\r\n\r\n"), 18, 0);
}

TEST(method_with_non_token_byte_rejected) {
  std::string s = "GE\"T / HTTP/1.1\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 2);
}

TEST(control_byte_in_target_rejected) {
  std::string s = std::string("GET /\x01 HTTP/1.1\r\n\r\n");
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 5);
}

TEST(non_ascii_byte_in_target_rejected) {
  std::string s = std::string("GET /\xe9 HTTP/1.1\r\n\r\n");
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 5);
}

TEST(nul_byte_before_end_is_error_not_incomplete) {
  // Eager syntax: the NUL cannot belong to any valid request, so the answer
  // is an error even though the input stops early.
  std::string s = std::string("GET /\0", 6);
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 5);
}

TEST(cr_where_space_is_required_is_error_at_end_of_input) {
  // The CR after "HTTP/1.1" cannot be SP, whatever follows it, so the error is
  // decided now and does not wait for the LF.
  std::string s = "POST /a/b?x=1HTTP/1.1\r";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, s.find('\r'));
}

TEST(cr_at_end_of_input_is_incomplete) {
  // A CR at the end may still be followed by an LF, so nothing is broken yet.
  EXPECT_INCOMPLETE(std::string("GET / HTTP/1.1\r"));
}

TEST(truncated_request_line_is_incomplete) { EXPECT_INCOMPLETE(std::string("GET /")); }

TEST(bytes_after_request_are_not_read) {
  // G-05: the first request ends at its empty line. The rest is the next request.
  std::string first = "GET / HTTP/1.1\r\n\r\n";
  EXPECT_COMPLETE(first + "GET /x HTTP/1.1\r\n\r\n", first.size(), 0);
}

TEST(tchar_punctuation_in_method_accepted) {
  // RL-02: the method is a token, so '*' and '!' are valid method bytes.
  std::string s = "G*!T / HTTP/1.1\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(htab_in_request_target_rejected) {
  // RL-03: the target is 0x21 to 0x7E only. HTAB is not allowed even though
  // it is a valid byte of the field grammar.
  std::string s = "GET /a\tb HTTP/1.1\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadRequestLine, 6);
}
