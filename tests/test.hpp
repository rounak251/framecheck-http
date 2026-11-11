// A small test runner. Each test is written as TEST(name). scripts/check_threats.sh
// looks for the text TEST(name) to confirm that a threat's test exists, so keep
// that spelling.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "framecheck/parser.hpp"

namespace fctest {

using TestFn = void (*)();

struct Registrar {
  Registrar(const char* name, TestFn fn);
};

// Counters are shared by all test files. test_main.cpp prints them.
int& failed_checks();
int& failed_tests();
const char*& current_test();

// The bytes are copied into a heap buffer of exactly the input size. A
// std::string keeps a NUL after its last byte, so a read one byte past the end
// would go unnoticed under ASan. With this copy it is a heap-buffer-overflow.
inline framecheck::Result run(const std::string& input) {
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[input.size()]);
  std::memcpy(buffer.get(), input.data(), input.size());
  return framecheck::parse(buffer.get(), input.size());
}

std::string describe(const framecheck::Result& r);

// Runs every test in the binary. Returns the process exit code.
int run_all();

}  // namespace fctest

#define TEST(name)                                                  \
  static void test_##name();                                        \
  static const fctest::Registrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++fctest::failed_checks();                                                 \
    }                                                                            \
  } while (0)

#define EXPECT_ERROR(input, cls, offset)                                         \
  do {                                                                           \
    framecheck::Result r_ = fctest::run(input);                                  \
    if (r_.kind != framecheck::Result::Kind::Error ||                            \
        r_.error_class != (cls) || r_.error_offset != (offset)) {                \
      std::fprintf(stderr, "  %s:%d: expected error %s at %zu, got %s\n",       \
                   __FILE__, __LINE__, framecheck::error_class_name(cls),        \
                   static_cast<size_t>(offset), fctest::describe(r_).c_str());   \
      ++fctest::failed_checks();                                                 \
    }                                                                            \
  } while (0)

#define EXPECT_COMPLETE(input, consumed_value, body_value)                       \
  do {                                                                           \
    framecheck::Result r_ = fctest::run(input);                                  \
    if (r_.kind != framecheck::Result::Kind::Complete ||                         \
        r_.consumed != (consumed_value) || r_.body_length != (body_value)) {     \
      std::fprintf(stderr, "  %s:%d: expected complete %zu/%llu, got %s\n",      \
                   __FILE__, __LINE__, static_cast<size_t>(consumed_value),      \
                   static_cast<unsigned long long>(body_value),                  \
                   fctest::describe(r_).c_str());                                \
      ++fctest::failed_checks();                                                 \
    }                                                                            \
  } while (0)

#define EXPECT_INCOMPLETE(input)                                                 \
  do {                                                                           \
    framecheck::Result r_ = fctest::run(input);                                  \
    if (r_.kind != framecheck::Result::Kind::Incomplete) {                       \
      std::fprintf(stderr, "  %s:%d: expected incomplete, got %s\n",            \
                   __FILE__, __LINE__, fctest::describe(r_).c_str());            \
      ++fctest::failed_checks();                                                 \
    }                                                                            \
  } while (0)
