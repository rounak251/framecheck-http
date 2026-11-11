// A small JSON reader for harness input lines.
//
// The harness only needs two string members (id and hex). So the reader checks
// that the whole line is one JSON object, keeps the string-valued members of
// the top-level object, and checks but skips every other value.
#pragma once

#include <map>
#include <string>
#include <string_view>

namespace framecheck::harness {

// Returns false if text is not exactly one JSON object (surrounding whitespace
// is allowed). On success, members gets every top-level member whose value is
// a string. If a key repeats, the last value wins.
bool read_string_members(std::string_view text, std::map<std::string, std::string>& members);

}  // namespace framecheck::harness
