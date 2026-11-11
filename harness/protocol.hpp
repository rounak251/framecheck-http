// The stdin/stdout protocol from docs/harness-protocol.md, version 1.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "framecheck/parser.hpp"

namespace framecheck::harness {

// Handles one input line. If the line is blank, nothing is appended and the
// function returns false. Otherwise it appends exactly one output line,
// including its newline, and returns true.
bool handle_line(std::string_view line, std::string& out);

// The output formatters are separate so that tests can check their exact text.
std::string format_result(const std::optional<std::string>& id, const Result& result);
std::string format_bad_input(const std::optional<std::string>& id);

}  // namespace framecheck::harness
