#include "protocol.hpp"

#include <cstdint>
#include <map>
#include <vector>

#include "json_lite.hpp"

namespace framecheck::harness {
namespace {

bool is_blank(std::string_view line) {
  for (char c : line) {
    if (c != ' ' && c != '\t' && c != '\r') return false;
  }
  return true;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Hex is only 0-9, a-f and A-F, with an even length. The empty string is zero bytes.
bool decode_hex(const std::string& hex, std::vector<uint8_t>& bytes) {
  if (hex.size() % 2 != 0) return false;
  bytes.clear();
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    int hi = hex_digit(hex[i]);
    int lo = hex_digit(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    bytes.push_back(static_cast<uint8_t>(hi * 16 + lo));
  }
  return true;
}

// JSON string escaping for the id. Control bytes become \u00XX. Bytes of 0x80
// and above are copied as they are, because the input was already read as bytes.
std::string json_string(const std::string& s) {
  static const char kHex[] = "0123456789abcdef";
  std::string out = "\"";
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c < 0x20) {
      out += "\\u00";
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0xF]);
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  out += "\"";
  return out;
}

std::string json_id(const std::optional<std::string>& id) {
  return id ? json_string(*id) : std::string("null");
}

}  // namespace

std::string format_result(const std::optional<std::string>& id, const Result& result) {
  bool complete = result.kind == Result::Kind::Complete;
  bool error = result.kind == Result::Kind::Error;
  const char* status = complete ? "complete" : (error ? "error" : "incomplete");

  // The keys are in alphabetical order, as the protocol asks.
  std::string s = "{";
  s += "\"body_length\":" + (complete ? std::to_string(result.body_length) : std::string("null"));
  s += ",\"close\":" + std::string(error ? "true" : "false");
  s += ",\"consumed\":" + (complete ? std::to_string(result.consumed) : std::string("null"));
  s += ",\"error_class\":" + (error ? json_string(error_class_name(result.error_class)) : std::string("null"));
  s += ",\"error_offset\":" + (error ? std::to_string(result.error_offset) : std::string("null"));
  s += ",\"id\":" + json_id(id);
  s += ",\"status\":\"" + std::string(status) + "\"";
  s += "}";
  return s;
}

std::string format_bad_input(const std::optional<std::string>& id) {
  return "{\"body_length\":null,\"close\":false,\"consumed\":null,\"error_class\":null,"
         "\"error_offset\":null,\"id\":" +
         json_id(id) + ",\"status\":\"bad-input\"}";
}

bool handle_line(std::string_view line, std::string& out) {
  if (is_blank(line)) return false;

  std::map<std::string, std::string> members;
  if (!read_string_members(line, members)) {
    out += format_bad_input(std::nullopt) + "\n";
    return true;
  }

  std::optional<std::string> id;
  auto id_it = members.find("id");
  if (id_it != members.end()) id = id_it->second;

  std::vector<uint8_t> bytes;
  auto hex_it = members.find("hex");
  if (hex_it == members.end() || !decode_hex(hex_it->second, bytes)) {
    out += format_bad_input(id) + "\n";
    return true;
  }

  Result result = parse(bytes.data(), bytes.size());
  out += format_result(id, result) + "\n";
  return true;
}

}  // namespace framecheck::harness
