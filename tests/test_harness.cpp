// The stdin harness: JSON reading, hex decoding and the output format.
#include <string>

#include "harness/protocol.hpp"
#include "test.hpp"

using framecheck::harness::handle_line;

namespace {

// Runs one input line through the harness and returns the output line.
std::string harness_out(const std::string& line) {
  std::string out;
  handle_line(line, out);
  return out;
}

}  // namespace

TEST(harness_blank_line_no_output) {
  std::string out;
  CHECK(!handle_line("", out));
  CHECK(!handle_line("  \t\r", out));
  CHECK(out.empty());
}

TEST(harness_valid_case_exact_output) {
  // The example from docs/harness-protocol.md, example 1.
  std::string out = harness_out(
      "{\"id\":\"ex-1\",\"hex\":\"504f5354202f20485454502f312e310d0a436f6e74656e742d4c656e6774683a20350d0a0d0a68656c6c6f474554202f\"}");
  CHECK(out == "{\"body_length\":5,\"close\":false,\"consumed\":43,\"error_class\":null,"
               "\"error_offset\":null,\"id\":\"ex-1\",\"status\":\"complete\"}\n");
}

TEST(harness_error_has_close_true) {
  // CN-01: every error sets close to true.
  std::string out = harness_out("{\"id\":\"ex-3\",\"hex\":\"474554202f20485454502f312e310a\"}");
  CHECK(out == "{\"body_length\":null,\"close\":true,\"consumed\":null,\"error_class\":\"bare-lf\","
               "\"error_offset\":14,\"id\":\"ex-3\",\"status\":\"error\"}\n");
}

TEST(harness_incomplete_output) {
  std::string out = harness_out("{\"id\":\"i\",\"hex\":\"474554\"}");
  CHECK(out == "{\"body_length\":null,\"close\":false,\"consumed\":null,\"error_class\":null,"
               "\"error_offset\":null,\"id\":\"i\",\"status\":\"incomplete\"}\n");
}

TEST(harness_empty_hex_is_zero_bytes) {
  std::string out = harness_out("{\"id\":\"e\",\"hex\":\"\"}");
  CHECK(out.find("\"status\":\"incomplete\"") != std::string::npos);
}

TEST(harness_odd_hex_length_is_bad_input_with_id) {
  std::string out = harness_out("{\"id\":\"odd\",\"hex\":\"474\"}");
  CHECK(out == "{\"body_length\":null,\"close\":false,\"consumed\":null,\"error_class\":null,"
               "\"error_offset\":null,\"id\":\"odd\",\"status\":\"bad-input\"}\n");
}

TEST(harness_non_hex_character_is_bad_input) {
  std::string out = harness_out("{\"id\":\"x\",\"hex\":\"4g\"}");
  CHECK(out.find("\"status\":\"bad-input\"") != std::string::npos);
}

TEST(harness_missing_hex_is_bad_input) {
  std::string out = harness_out("{\"id\":\"x\"}");
  CHECK(out.find("\"status\":\"bad-input\"") != std::string::npos);
  CHECK(out.find("\"id\":\"x\"") != std::string::npos);
}

TEST(harness_not_an_object_is_bad_input_with_null_id) {
  std::string out = harness_out("[1,2,3]");
  CHECK(out == "{\"body_length\":null,\"close\":false,\"consumed\":null,\"error_class\":null,"
               "\"error_offset\":null,\"id\":null,\"status\":\"bad-input\"}\n");
}

TEST(harness_non_string_id_is_null) {
  std::string out = harness_out("{\"id\":7,\"hex\":\"zz\"}");
  CHECK(out.find("\"id\":null") != std::string::npos);
}

TEST(harness_trailing_garbage_after_object_is_bad_input) {
  std::string out = harness_out("{\"id\":\"t\",\"hex\":\"\"} x");
  CHECK(out.find("\"status\":\"bad-input\"") != std::string::npos);
}

TEST(harness_id_with_quote_and_control_is_escaped) {
  std::string out = harness_out("{\"id\":\"a\\\"b\\u0001\",\"hex\":\"\"}");
  CHECK(out.find("\"id\":\"a\\\"b\\u0001\"") != std::string::npos);
}

TEST(harness_unknown_keys_are_checked_and_ignored) {
  std::string out = harness_out("{\"id\":\"k\",\"extra\":[1,{\"n\":null}],\"hex\":\"\",\"f\":-1.5e+3}");
  CHECK(out.find("\"status\":\"incomplete\"") != std::string::npos);
}

TEST(harness_json_rejects_raw_control_in_string) {
  std::string out = harness_out(std::string("{\"id\":\"a\tb\",\"hex\":\"\"}"));
  CHECK(out.find("\"status\":\"bad-input\"") != std::string::npos);
}

TEST(harness_json_rejects_surrogate_escape) {
  std::string out = harness_out("{\"id\":\"\\ud800\",\"hex\":\"\"}");
  CHECK(out.find("\"status\":\"bad-input\"") != std::string::npos);
}

TEST(harness_duplicate_string_key_last_value_wins) {
  // The harness keeps the last value of a repeated key, as the JSON in the
  // oracle does. Here the last hex is empty, so the case is incomplete.
  std::string out = harness_out("{\"id\":\"d\",\"hex\":\"474554\",\"hex\":\"\"}");
  CHECK(out.find("\"status\":\"incomplete\"") != std::string::npos);
}

TEST(harness_duplicate_key_last_non_string_drops_the_member) {
  // The last hex is a number, so there is no usable hex. The oracle reads the
  // same object as having a non-string hex and says bad-input.
  std::string out = harness_out("{\"id\":\"dup\",\"hex\":\"474554\",\"hex\":5}");
  CHECK(out == "{\"body_length\":null,\"close\":false,\"consumed\":null,\"error_class\":null,"
               "\"error_offset\":null,\"id\":\"dup\",\"status\":\"bad-input\"}\n");
}
