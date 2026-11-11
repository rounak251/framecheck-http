// Framing decisions: FR-01 to FR-11, run after the header section.
#include <string>

#include "test.hpp"

using framecheck::ErrorClass;

namespace {
const std::string kPost = "POST / HTTP/1.1\r\n";
}

TEST(cl_and_te_together_rejected) {
  // FR-01. This is the classic request smuggling shape.
  std::string s = kPost + "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::ClTeConflict, s.find("Transfer-Encoding"));
}

TEST(cl_and_te_in_either_order_rejected) {
  std::string s = kPost + "Transfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::ClTeConflict, s.find("Content-Length"));
}

TEST(cl_te_conflict_waits_for_header_end) {
  // The framing rules need the whole header section, so this is incomplete.
  EXPECT_INCOMPLETE(kPost + "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n");
}

TEST(duplicate_cl_different_values_rejected) {
  std::string s = kPost + "Content-Length: 5\r\nContent-Length: 6\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::DuplicateContentLength, s.find("Content-Length", s.find("Content-Length") + 1));
}

TEST(duplicate_cl_identical_values_accepted) {
  // FR-02. Identical lines are accepted and the shared value is the length.
  std::string s = kPost + "Content-Length: 5\r\nContent-Length: 5\r\n\r\nhello";
  EXPECT_COMPLETE(s, s.size(), 5);
}

TEST(duplicate_cl_same_number_with_leading_zero_accepted) {
  // The spec table says "different decimal values". "05" and "5" are the same
  // decimal value, so they are accepted here.
  std::string s = kPost + "Content-Length: 5\r\nContent-Length: 05\r\n\r\nhello";
  EXPECT_COMPLETE(s, s.size(), 5);
}

TEST(content_length_trailing_ows_accepted) {
  // HF-04. Trailing SP and HTAB are not part of the value, so "5 \t" is 5.
  std::string s = kPost + "Content-Length: 5 \t\r\n\r\nhello";
  EXPECT_COMPLETE(s, s.size(), 5);
}

TEST(header_name_with_framing_name_as_prefix_is_not_framing) {
  // The whole name must match. Content-Length-Note and Transfer-Encoding-Note are
  // ordinary headers, so there is no body and the message ends at the empty line.
  std::string head = kPost + "Content-Length-Note: 5\r\nTransfer-Encoding-Note: chunked\r\n\r\n";
  EXPECT_COMPLETE(head + "5\r\nhello", head.size(), 0);
}

TEST(cl_comma_list_rejected) {
  // FR-03. The spec table rejects a comma list even when the values match.
  std::string s = kPost + "Content-Length: 5, 5\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadContentLength, s.find("Content-Length"));
}

TEST(cl_with_plus_sign_rejected) {
  std::string s = kPost + "Content-Length: +5\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadContentLength, s.find("Content-Length"));
}

TEST(cl_with_minus_sign_rejected) {
  std::string s = kPost + "Content-Length: -5\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadContentLength, s.find("Content-Length"));
}

TEST(cl_empty_value_rejected) {
  std::string s = kPost + "Content-Length:\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadContentLength, s.find("Content-Length"));
}

TEST(cl_overflow_rejected) {
  std::string s = kPost + "Content-Length: 9223372036854775808\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadContentLength, s.find("Content-Length"));
}

TEST(cl_at_cap_accepted_then_incomplete) {
  // 2^63 - 1 is the largest accepted value. The body is not there yet.
  EXPECT_INCOMPLETE(kPost + "Content-Length: 9223372036854775807\r\n\r\n");
}

TEST(cl_case_insensitive_name) {
  std::string s = kPost + "content-LENGTH: 3\r\n\r\nabc";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(duplicate_te_rejected) {
  // FR-04. The second line is the one reported.
  std::string s = kPost + "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding", s.find("Transfer-Encoding") + 1));
}

TEST(te_final_coding_not_chunked_rejected) {
  // FR-06. "chunked, gzip" has a last coding of gzip.
  std::string s = kPost + "Transfer-Encoding: chunked, gzip\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(xchunked_rejected) {
  std::string s = kPost + "Transfer-Encoding: xchunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(chunked_then_identity_rejected) {
  std::string s = kPost + "Transfer-Encoding: chunked, identity\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(chunked_twice_rejected) {
  // FR-07.
  std::string s = kPost + "Transfer-Encoding: chunked, chunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(te_with_parameter_rejected) {
  // FR-05. Parameters are rejected even on chunked.
  std::string s = kPost + "Transfer-Encoding: chunked;q=1\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(te_empty_element_rejected) {
  std::string s = kPost + "Transfer-Encoding: gzip,,chunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(te_trailing_comma_rejected) {
  std::string s = kPost + "Transfer-Encoding: chunked,\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(te_http10_rejected) {
  // FR-09. HTTP/1.0 with Transfer-Encoding is faulty framing.
  std::string s = "POST / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding"));
}

TEST(te_gzip_then_chunked_accepted) {
  // FR-08. Codings before the final chunked are framed, not decoded.
  std::string s = kPost + "Transfer-Encoding: gzip, chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(te_chunked_is_case_insensitive) {
  std::string s = kPost + "Transfer-Encoding: Chunked\r\n\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(no_framing_means_empty_body) {
  // FR-10. Without CL or TE, consumed ends at the header section.
  std::string head = "GET / HTTP/1.1\r\nHost: a\r\n\r\n";
  EXPECT_COMPLETE(head + "GET /next HTTP/1.1\r\n\r\n", head.size(), 0);
}

TEST(content_length_body_is_read_exactly) {
  // FR-11. Bytes after the body are the next request and are not consumed.
  std::string head = kPost + "Content-Length: 5\r\n\r\n";
  EXPECT_COMPLETE(head + "helloGET / HTTP/1.1\r\n\r\n", head.size() + 5, 5);
}

TEST(content_length_short_body_is_incomplete) {
  EXPECT_INCOMPLETE(kPost + "Content-Length: 5\r\n\r\nhel");
}

TEST(content_length_zero_is_complete) {
  std::string s = kPost + "Content-Length: 0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(header_section_incomplete_has_no_framing_answer) {
  // Without the empty line the framing rules have not run, so only the
  // syntax of the lines is checked.
  EXPECT_INCOMPLETE(kPost + "Content-Length: 5\r\n");
}

TEST(duplicate_te_with_gzip_chunked_then_chunked_rejected) {
  // FR-04 comes before FR-07. The second TE line is the reported one.
  std::string s = kPost + "Transfer-Encoding: gzip, chunked\r\nTransfer-Encoding: chunked\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTransferEncoding, s.find("Transfer-Encoding", s.find("Transfer-Encoding") + 1));
}
