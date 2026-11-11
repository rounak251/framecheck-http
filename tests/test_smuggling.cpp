// Runs the smuggling corpus and the valid seed corpus. The directories are read
// relative to the project root, which is where the Makefile runs the tests.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "harness/protocol.hpp"
#include "test.hpp"

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> corpus_files(const char* dir) {
  std::vector<fs::path> files;
  if (!fs::is_directory(dir)) return files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".http") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string to_hex(const std::string& bytes) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : bytes) {
    out.push_back(kHex[c >> 4]);
    out.push_back(kHex[c & 0xF]);
  }
  return out;
}

}  // namespace

// Every case must be an error, and the harness line for it must say close.
// The spec table says a rejected framing gets a 400 and a closed connection.
TEST(smuggling_corpus_all_rejected_with_close) {
  std::vector<fs::path> files = corpus_files("corpus/smuggling");
  CHECK(files.size() >= 40);
  for (const fs::path& path : files) {
    std::string bytes = read_file(path);
    framecheck::Result r = fctest::run(bytes);
    if (r.kind != framecheck::Result::Kind::Error) {
      std::fprintf(stderr, "  %s: not rejected: %s\n", path.c_str(), fctest::describe(r).c_str());
      ++fctest::failed_checks();
      continue;
    }
    std::string line = "{\"id\":\"" + path.stem().string() + "\",\"hex\":\"" + to_hex(bytes) + "\"}";
    std::string out;
    framecheck::harness::handle_line(line, out);
    if (out.find("\"close\":true") == std::string::npos) {
      std::fprintf(stderr, "  %s: harness output lacks close:true\n", path.c_str());
      ++fctest::failed_checks();
    }
  }
}

// The valid seeds are the inputs the fuzzer mutates. They must parse cleanly.
TEST(valid_corpus_all_complete) {
  std::vector<fs::path> files = corpus_files("corpus/valid");
  CHECK(!files.empty());
  for (const fs::path& path : files) {
    std::string bytes = read_file(path);
    framecheck::Result r = fctest::run(bytes);
    if (r.kind != framecheck::Result::Kind::Complete) {
      std::fprintf(stderr, "  %s: expected complete, got %s\n", path.c_str(), fctest::describe(r).c_str());
      ++fctest::failed_checks();
    }
  }
}
