#include "json_lite.hpp"

#include <optional>

namespace framecheck::harness {
namespace {

// Nesting limit. It keeps a hostile line from using up the stack.
constexpr int kMaxDepth = 64;

class Reader {
 public:
  explicit Reader(std::string_view text) : s_(text) {}

  void skip_ws() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool at_end() const { return pos_ == s_.size(); }

  bool peek_is(char c) const { return pos_ < s_.size() && s_[pos_] == c; }

  bool consume(char c) {
    if (!peek_is(c)) return false;
    ++pos_;
    return true;
  }

  // Parses a value. If the value is a string, out holds it. Otherwise out is
  // empty.
  bool parse_value(int depth, std::optional<std::string>& out) {
    out.reset();
    if (depth > kMaxDepth) return false;
    skip_ws();
    if (at_end()) return false;
    char c = s_[pos_];
    if (c == '"') {
      std::string str;
      if (!parse_string(str)) return false;
      out = std::move(str);
      return true;
    }
    if (c == '{') return parse_object(depth + 1, nullptr);
    if (c == '[') return parse_array(depth + 1);
    if (c == 't') return literal("true");
    if (c == 'f') return literal("false");
    if (c == 'n') return literal("null");
    return parse_number();
  }

  // Parses an object. members is null for nested objects, whose values are
  // checked and then dropped.
  bool parse_object(int depth, std::map<std::string, std::string>* members) {
    if (depth > kMaxDepth) return false;
    ++pos_;  // '{'
    skip_ws();
    if (consume('}')) return true;
    for (;;) {
      skip_ws();
      std::string key;
      if (!parse_string(key)) return false;
      skip_ws();
      if (!consume(':')) return false;
      std::optional<std::string> value;
      if (!parse_value(depth + 1, value)) return false;
      // The last value of a repeated key wins, as in JSON. A non-string last
      // value removes the member, so the oracle and this reader agree.
      if (members) {
        if (value) {
          (*members)[key] = *value;
        } else {
          members->erase(key);
        }
      }
      skip_ws();
      if (consume(',')) continue;
      return consume('}');
    }
  }

 private:
  bool parse_array(int depth) {
    if (depth > kMaxDepth) return false;
    ++pos_;  // '['
    skip_ws();
    if (consume(']')) return true;
    for (;;) {
      std::optional<std::string> ignored;
      if (!parse_value(depth + 1, ignored)) return false;
      skip_ws();
      if (consume(',')) continue;
      return consume(']');
    }
  }

  bool literal(const char* word) {
    std::string_view w(word);
    if (s_.substr(pos_, w.size()) != w) return false;
    pos_ += w.size();
    return true;
  }

  // JSON number grammar: -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?
  bool parse_number() {
    consume('-');
    if (consume('0')) {
      // A leading zero may not be followed by more digits.
    } else if (!digits()) {
      return false;
    }
    if (consume('.') && !digits()) return false;
    if (peek_is('e') || peek_is('E')) {
      ++pos_;
      if (!consume('+')) consume('-');
      if (!digits()) return false;
    }
    return true;
  }

  bool digits() {
    size_t start = pos_;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    return pos_ != start;
  }

  static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool read_code_unit(unsigned& unit) {
    if (pos_ + 4 > s_.size()) return false;
    unit = 0;
    for (int i = 0; i < 4; ++i) {
      int d = hex_digit(s_[pos_ + static_cast<size_t>(i)]);
      if (d < 0) return false;
      unit = unit * 16 + static_cast<unsigned>(d);
    }
    pos_ += 4;
    return true;
  }

  // Strings: control characters are not allowed. Raw bytes of 0x80 and above
  // are copied as they are. Surrogate \u escapes are rejected, because the
  // harness would have to pair them to make valid UTF-8.
  bool parse_string(std::string& out) {
    if (!consume('"')) return false;
    while (pos_ < s_.size()) {
      unsigned char c = static_cast<unsigned char>(s_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c < 0x20) return false;
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        ++pos_;
        continue;
      }
      ++pos_;  // backslash
      if (at_end()) return false;
      char e = s_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned unit = 0;
          if (!read_code_unit(unit)) return false;
          if (unit >= 0xD800 && unit <= 0xDFFF) return false;
          append_utf8(out, unit);
          break;
        }
        default:
          return false;
      }
    }
    return false;  // unterminated string
  }

  static void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  std::string_view s_;
  size_t pos_ = 0;
};

}  // namespace

bool read_string_members(std::string_view text, std::map<std::string, std::string>& members) {
  Reader reader(text);
  reader.skip_ws();
  if (!reader.peek_is('{')) return false;
  if (!reader.parse_object(1, &members)) return false;
  reader.skip_ws();
  return reader.at_end();
}

}  // namespace framecheck::harness
