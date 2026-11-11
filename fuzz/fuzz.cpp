// A small mutational fuzzer for the parser. Build it with ASan and UBSan (see
// the Makefile). The sanitizers catch memory and undefined-behaviour bugs. This
// file checks the rest:
//
//   1. A Complete answer for the input is still Complete when only the first
//      `consumed` bytes are given, with the same body length.
//   2. An Error answer has an offset inside the input.
//   3. If a prefix of the input is an Error, the whole input has the same Error.
//   4. No single parse takes more than one second.
//
// Usage: fuzz ITERATIONS SEED CORPUS_DIR...
// The seed corpus is every *.http file in the directories given. The same seed
// and corpus always give the same run.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "framecheck/parser.hpp"

namespace {

constexpr size_t kMaxInput = 4096;

// splitmix64. It is small, fast, and the same on every platform.
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  // A number in [0, n). Returns 0 when n is 0.
  size_t below(size_t n) { return n == 0 ? 0 : static_cast<size_t>(next() % n); }

 private:
  uint64_t state_;
};

// Pieces that are likely to matter to the parser. Splicing them in finds the
// boundary cases faster than single random bytes would.
const char* const kTokens[] = {
    "\r\n", "\r\n\r\n", "\n", "\r", " ", "\t", ":", ",", ";", "=", "\"", "\\",
    "Transfer-Encoding: chunked\r\n", "Content-Length: 5\r\n", "chunked", "gzip",
    "0\r\n\r\n", "1", "f", "ffffffffffffffff", "HTTP/1.1", "HTTP/1.0", "\x80", "\x00", "\x7f",
    "\x01", "3;a=\"b\"\r\n", "*", "]", " = ", "\t", "G*T ", "\"]]\"",
};

void insert_bytes(std::vector<uint8_t>& data, size_t at, const uint8_t* src, size_t n) {
  data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), src, src + n);
}

// One random edit. The seeds are used as a source for splices.
void mutate(std::vector<uint8_t>& data, Rng& rng, const std::vector<std::vector<uint8_t>>& seeds) {
  switch (rng.below(7)) {
    case 0:  // flip one bit
      if (!data.empty()) data[rng.below(data.size())] ^= static_cast<uint8_t>(1u << rng.below(8));
      break;
    case 1: {  // insert a token
      const char* tok = kTokens[rng.below(sizeof(kTokens) / sizeof(kTokens[0]))];
      const auto* bytes = reinterpret_cast<const uint8_t*>(tok);
      size_t n = std::char_traits<char>::length(tok);
      insert_bytes(data, rng.below(data.size() + 1), bytes, n);
      break;
    }
    case 2: {  // delete up to 8 bytes
      if (data.empty()) break;
      size_t at = rng.below(data.size());
      size_t n = 1 + rng.below(8);
      if (at + n > data.size()) n = data.size() - at;
      data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
                 data.begin() + static_cast<std::ptrdiff_t>(at + n));
      break;
    }
    case 3:  // overwrite one byte with any value
      if (!data.empty()) data[rng.below(data.size())] = static_cast<uint8_t>(rng.next());
      break;
    case 4: {  // splice a slice from another seed
      const std::vector<uint8_t>& other = seeds[rng.below(seeds.size())];
      if (other.empty()) break;
      size_t from = rng.below(other.size());
      size_t n = 1 + rng.below(other.size() - from);
      insert_bytes(data, rng.below(data.size() + 1), other.data() + from, n);
      break;
    }
    case 5:  // cut the input short
      data.resize(rng.below(data.size() + 1));
      break;
    case 6: {  // copy a slice of this input to another place in it
      if (data.empty()) break;
      size_t from = rng.below(data.size());
      size_t n = 1 + rng.below(data.size() - from);
      std::vector<uint8_t> piece(data.begin() + static_cast<std::ptrdiff_t>(from),
                                 data.begin() + static_cast<std::ptrdiff_t>(from + n));
      insert_bytes(data, rng.below(data.size() + 1), piece.data(), piece.size());
      break;
    }
  }
  if (data.size() > kMaxInput) data.resize(kMaxInput);
}

struct Stats {
  uint64_t complete = 0;
  uint64_t incomplete = 0;
  uint64_t error = 0;
  uint64_t findings = 0;
  double slowest_seconds = 0.0;
};

void finding(Stats& stats, const char* what, const std::vector<uint8_t>& data) {
  ++stats.findings;
  std::fprintf(stderr, "FINDING: %s (input %zu bytes)\n", what, data.size());
  for (size_t i = 0; i < data.size() && i < 256; ++i) std::fprintf(stderr, "%02x", data[i]);
  std::fprintf(stderr, "\n");
}

void check(const std::vector<uint8_t>& data, Rng& rng, Stats& stats) {
  using framecheck::Result;
  auto start = std::chrono::steady_clock::now();
  Result full = framecheck::parse(data.data(), data.size());
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (seconds > stats.slowest_seconds) stats.slowest_seconds = seconds;
  if (seconds > 1.0) finding(stats, "parse took more than one second", data);

  if (full.kind == Result::Kind::Complete) {
    ++stats.complete;
    if (full.consumed == 0 || full.consumed > data.size() || full.body_length > full.consumed) {
      finding(stats, "complete result has impossible consumed or body length", data);
    } else {
      Result again = framecheck::parse(data.data(), full.consumed);
      if (again.kind != Result::Kind::Complete || again.consumed != full.consumed ||
          again.body_length != full.body_length) {
        finding(stats, "complete answer changed when trailing bytes were removed", data);
      }
    }
  } else if (full.kind == Result::Kind::Error) {
    ++stats.error;
    if (full.error_offset >= data.size()) finding(stats, "error offset outside the input", data);
  } else {
    ++stats.incomplete;
  }

  // One random prefix. Only errors are compared, because a prefix that is
  // incomplete may still become anything.
  // A prefix that ends in CR can give a different class than the whole input,
  // because bare-cr depends on the byte after the CR. The offset must still match.
  size_t cut = rng.below(data.size() + 1);
  Result prefix = framecheck::parse(data.data(), cut);
  if (prefix.kind == Result::Kind::Error) {
    bool ends_in_cr = cut > 0 && data[cut - 1] == '\r';
    if (full.kind != Result::Kind::Error || full.error_offset != prefix.error_offset ||
        (!ends_in_cr && full.error_class != prefix.error_class)) {
      finding(stats, "error in a prefix was not the same error in the whole input", data);
    }
  }
}

bool load_file(const std::filesystem::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s ITERATIONS SEED CORPUS_DIR...\n", argv[0]);
    return 2;
  }
  const uint64_t iterations = std::strtoull(argv[1], nullptr, 10);
  const uint64_t seed = std::strtoull(argv[2], nullptr, 10);

  std::vector<std::vector<uint8_t>> seeds;
  for (int i = 3; i < argc; ++i) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(argv[i])) {
      if (entry.path().extension() == ".http") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
      std::vector<uint8_t> bytes;
      if (load_file(path, bytes)) seeds.push_back(bytes);
    }
  }
  if (seeds.empty()) {
    std::fprintf(stderr, "no seeds found\n");
    return 2;
  }

  Rng rng(seed);
  Stats stats;
  auto start = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < iterations; ++i) {
    std::vector<uint8_t> data = seeds[rng.below(seeds.size())];
    size_t edits = 1 + rng.below(4);
    for (size_t e = 0; e < edits; ++e) mutate(data, rng, seeds);
    check(data, rng, stats);
  }
  double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  std::printf("iterations=%llu seed=%llu seeds=%zu\n", static_cast<unsigned long long>(iterations),
              static_cast<unsigned long long>(seed), seeds.size());
  std::printf("outcomes: complete=%llu incomplete=%llu error=%llu\n",
              static_cast<unsigned long long>(stats.complete),
              static_cast<unsigned long long>(stats.incomplete),
              static_cast<unsigned long long>(stats.error));
  std::printf("seconds=%.3f iterations_per_second=%.0f slowest_parse_seconds=%.6f\n", total,
              total > 0 ? static_cast<double>(iterations) / total : 0.0, stats.slowest_seconds);
  std::printf("findings=%llu\n", static_cast<unsigned long long>(stats.findings));
  return stats.findings == 0 ? 0 : 1;
}
