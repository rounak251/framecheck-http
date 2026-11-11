// Reads one JSON case per line from stdin and writes one result per line to
// stdout. The protocol is in docs/harness-protocol.md. The differential runner
// is the only expected caller.
#include <cstdio>
#include <iostream>
#include <string>

#include "protocol.hpp"

int main() {
  std::ios::sync_with_stdio(false);
  std::string line;
  std::string out;
  while (std::getline(std::cin, line)) {
    out.clear();
    if (!framecheck::harness::handle_line(line, out)) continue;
    // Flush after each line. The runner reads the answers as they come.
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
  }
  return 0;
}
