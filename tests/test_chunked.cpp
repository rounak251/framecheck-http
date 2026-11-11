// Chunked transfer coding: CH-01 to CH-09.
#include <string>

#include "test.hpp"

using framecheck::ErrorClass;

namespace {
const std::string kHead = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
}

TEST(chunked_body_length_is_sum_of_chunks) {
  // CH-09. 3 + 2 octets.
  std::string body = "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
  std::string s = kHead + body;
  EXPECT_COMPLETE(s, s.size(), 5);
}

TEST(chunk_size_hex_digits_any_case) {
  // CH-01. 0x0A = 10 octets, written in upper and lower case.
  std::string data = "0123456789";
  std::string s = kHead + "0A\r\n" + data + "\r\n" + "a\r\n" + data + "\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 20);
}

TEST(chunk_size_leading_zeros_accepted) {
  std::string s = kHead + "000003\r\nabc\r\n0000\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_size_overflow_rejected) {
  // CH-02. The 16th digit is the first one that takes the value past 2^63 - 1.
  std::string s = kHead + "8000000000000000\r\n";
  EXPECT_ERROR(s, ErrorClass::ChunkSizeOverflow, s.find("8000000000000000") + 15);
}

TEST(chunk_size_very_long_run_of_digits_overflow_not_crash) {
  std::string run(200, 'f');
  std::string s = kHead + run + "\r\n";
  EXPECT_ERROR(s, ErrorClass::ChunkSizeOverflow, s.find(run) + 15);
}

TEST(chunk_size_at_cap_is_incomplete_not_overflow) {
  // 2^63 - 1 is the largest size CH-02 allows. The parser then waits for that
  // many octets, which the input does not have, so the answer is incomplete.
  // The test checks only that the cap is not an overflow error.
  EXPECT_INCOMPLETE(kHead + "7fffffffffffffff\r\n");
}

TEST(chunk_size_without_digits_rejected) {
  std::string s = kHead + "\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkSize, s.find("\r\n", kHead.size()));
}

TEST(chunk_size_space_before_crlf_rejected) {
  // CH-03. Whitespace after the size with no ';' before the CRLF is an error.
  std::string s = kHead + "3 \r\nabc\r\n0\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkSize, s.find('\r', kHead.size() + 1));
}

TEST(chunk_ext_accepted) {
  std::string s = kHead + "3;name=value\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_quoted_value_accepted) {
  std::string s = kHead + "3;a=\"x y\"\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_quoted_pair_accepted) {
  std::string s = kHead + "3;a=\"x\\\"y\"\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_whitespace_before_semicolon_accepted) {
  std::string s = kHead + "3 ;a\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_obs_text_in_quoted_string_rejected) {
  // CH-03. obs-text is rejected inside quoted-string.
  std::string s = kHead + std::string("3;a=\"\xe9\"\r\n");
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, s.find('\xe9'));
}

TEST(chunk_ext_quoted_pair_with_control_byte_rejected) {
  std::string s = kHead + std::string("3;a=\"\\\x01\"\r\n");
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, s.find('\x01'));
}

TEST(chunk_ext_without_name_rejected) {
  std::string s = kHead + "3;=x\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, s.find('=', kHead.size()));
}

TEST(chunk_ext_whitespace_after_name_before_crlf_rejected) {
  std::string s = kHead + "3;a \r\nabc\r\n0\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, s.find('\r', kHead.size() + 4));
}

TEST(chunk_data_missing_crlf_rejected) {
  // CH-05. The 4th byte of "abcX" is where CRLF should be.
  std::string s = kHead + "3\r\nabcX0\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkDataTerminator, s.find('X'));
}

TEST(chunk_data_may_contain_crlf) {
  // CH-05. The 4 octets are a CR, LF, a and b. They are data, not framing.
  std::string s = kHead + "4\r\na\r\nb\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 4);
}

TEST(zero_chunk_with_extension_accepted) {
  std::string s = kHead + "0;end=1\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(trailer_field_accepted) {
  std::string s = kHead + "0\r\nX-Checksum: abc\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 0);
}

TEST(trailer_content_length_rejected) {
  // CH-08. A framing field in a trailer is refused at the start of the line.
  std::string s = kHead + "0\r\nContent-Length: 3\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTrailer, s.find("Content-Length"));
}

TEST(trailer_transfer_encoding_rejected_before_line_ends) {
  // The name is complete at the colon, so the error does not wait for the CRLF.
  std::string s = kHead + "0\r\nTransfer-Encoding: x";
  EXPECT_ERROR(s, ErrorClass::BadTrailer, s.rfind("Transfer-Encoding"));
}

TEST(trailer_bad_name_rejected) {
  std::string s = kHead + "0\r\nBad Name: x\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTrailer, s.find(' ', s.find("Bad")));
}

TEST(trailer_obs_fold_rejected) {
  std::string s = kHead + "0\r\nX: a\r\n b\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadTrailer, s.find(" b"));
}

TEST(trailer_bare_lf_rejected) {
  std::string s = kHead + "0\r\nX: a\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BareLf, s.find('\n', kHead.size() + 3));
}

TEST(chunked_truncated_is_incomplete) {
  EXPECT_INCOMPLETE(kHead + "3\r\nab");
}

TEST(chunked_bytes_after_last_chunk_not_read) {
  std::string s = kHead + "0\r\n\r\n";
  EXPECT_COMPLETE(s + std::string("GARBAGE\0", 8), s.size(), 0);
}

TEST(chunk_ext_quoted_string_allows_right_bracket) {
  // CH-03 and RFC 9110 section 5.6.4: qdtext includes ']' (0x5D).
  std::string s = kHead + "3;a=\"]]\"\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_whitespace_around_equals_accepted) {
  // CH-03: the grammar is BWS "=" BWS, so spaces on both sides of '=' are fine.
  std::string s = kHead + "3;a = b\r\nabc\r\n0\r\n\r\n";
  EXPECT_COMPLETE(s, s.size(), 3);
}

TEST(chunk_ext_empty_value_rejected) {
  // CH-03: chunk-ext-val is a token or a quoted-string, and an empty value is neither.
  std::string s = kHead + "3;a=\r\nabc\r\n0\r\n\r\n";
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, kHead.size() + 4);
}

TEST(chunk_ext_junk_after_value_is_bad_chunk_ext) {
  // CH-03: a malformed extension is bad-chunk-ext, even when a size came before it.
  std::string s = kHead + std::string("3;a=b\x01\r\nabc\r\n0\r\n\r\n");
  EXPECT_ERROR(s, ErrorClass::BadChunkExt, kHead.size() + 5);
}

TEST(chunk_ext_quoted_string_ends_after_backslash_is_incomplete) {
  // The quoted-pair needs one more byte. The input ends on the backslash, so
  // the answer is incomplete. A read past the end must not happen (ASan checks it).
  std::string s = kHead + "3;a=\"\\";
  EXPECT_INCOMPLETE(s);
}

TEST(chunk_size_leading_zero_then_cap_accepted_then_incomplete) {
  // CH-01 and CH-02. Leading zeros do not count towards the value, so a
  // 17-digit run that is 2^63 - 1 in value is accepted.
  EXPECT_INCOMPLETE(kHead + "07fffffffffffffff\r\n");
}
