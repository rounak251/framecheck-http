// Runs every registered test and prints one line per test.
#include <cstdio>
#include <string>
#include <vector>

#include "test.hpp"

namespace fctest {
namespace {

struct Entry {
  const char* name;
  TestFn fn;
};

std::vector<Entry>& registry() {
  static std::vector<Entry> entries;
  return entries;
}

}  // namespace

Registrar::Registrar(const char* name, TestFn fn) { registry().push_back({name, fn}); }

int& failed_checks() {
  static int count = 0;
  return count;
}

int& failed_tests() {
  static int count = 0;
  return count;
}

const char*& current_test() {
  static const char* name = "";
  return name;
}

std::string describe(const framecheck::Result& r) {
  using framecheck::Result;
  switch (r.kind) {
    case Result::Kind::Complete:
      return "complete consumed=" + std::to_string(r.consumed) +
             " body=" + std::to_string(r.body_length);
    case Result::Kind::Incomplete:
      return "incomplete";
    case Result::Kind::Error:
      return std::string("error ") + framecheck::error_class_name(r.error_class) +
             " at " + std::to_string(r.error_offset);
  }
  return "?";
}

int run_all() {
  int total = 0;
  for (const Entry& e : registry()) {
    current_test() = e.name;
    int before = failed_checks();
    e.fn();
    ++total;
    if (failed_checks() != before) {
      ++failed_tests();
      std::printf("FAIL %s\n", e.name);
    } else {
      std::printf("ok   %s\n", e.name);
    }
  }
  std::printf("%d tests, %d failed\n", total, failed_tests());
  return failed_tests() == 0 ? 0 : 1;
}

}  // namespace fctest

int main() { return fctest::run_all(); }
