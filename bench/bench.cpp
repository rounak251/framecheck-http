// Single-thread parse throughput on a pipelined stream of valid requests.
//
// Usage: bench [--quick] --source-sha256 HASH --hardware TEXT --compiler TEXT
//
// The full run parses 1 GiB. --quick parses 16 MiB and marks the output as a
// smoke run, so its numbers must not be quoted anywhere. The output is one JSON
// object on stdout.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "framecheck/parser.hpp"

namespace {

const std::string kRequestHead =
    "POST /bench HTTP/1.1\r\nHost: example.com\r\nUser-Agent: framecheck-bench\r\n"
    "Content-Length: 64\r\n\r\n";

// Escapes the few characters that could break the JSON string. The arguments
// come from the Makefile, so this is a safety net, not a parser.
std::string json_text(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  std::string sha = "unknown";
  std::string hardware = "unknown";
  std::string compiler = "unknown";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) {
      quick = true;
    } else if (std::strcmp(argv[i], "--source-sha256") == 0 && i + 1 < argc) {
      sha = argv[++i];
    } else if (std::strcmp(argv[i], "--hardware") == 0 && i + 1 < argc) {
      hardware = argv[++i];
    } else if (std::strcmp(argv[i], "--compiler") == 0 && i + 1 < argc) {
      compiler = argv[++i];
    } else {
      std::fprintf(stderr, "usage: bench [--quick] --source-sha256 H --hardware T --compiler T\n");
      return 2;
    }
  }

  const size_t target_bytes = quick ? (size_t{16} << 20) : (size_t{1} << 30);
  const size_t copies = target_bytes / (kRequestHead.size() + 64);
  std::string stream;
  stream.reserve(copies * (kRequestHead.size() + 64));
  for (size_t i = 0; i < copies; ++i) {
    stream += kRequestHead;
    stream.append(64, 'x');
  }

  // The stream is built before the clock starts, so only parsing is timed.
  const auto* data = reinterpret_cast<const uint8_t*>(stream.data());
  uint64_t messages = 0;
  uint64_t body_checksum = 0;
  size_t offset = 0;
  auto start = std::chrono::steady_clock::now();
  while (offset < stream.size()) {
    framecheck::Result r = framecheck::parse(data + offset, stream.size() - offset);
    if (r.kind != framecheck::Result::Kind::Complete) {
      std::fprintf(stderr, "bench: a valid request did not parse at offset %zu\n", offset);
      return 1;
    }
    offset += r.consumed;
    body_checksum += r.body_length;
    ++messages;
  }
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  double mb_per_s = static_cast<double>(stream.size()) / 1e6 / seconds;

  std::printf("{\n");
  std::printf("  \"mode\": \"%s\",\n", quick ? "quick-smoke" : "full");
  std::printf("  \"placeholder\": %s,\n", quick ? "true" : "false");
  std::printf("  \"stream_bytes\": %zu,\n", stream.size());
  std::printf("  \"messages\": %llu,\n", static_cast<unsigned long long>(messages));
  std::printf("  \"body_bytes_checksum\": %llu,\n", static_cast<unsigned long long>(body_checksum));
  std::printf("  \"seconds\": %.6f,\n", seconds);
  std::printf("  \"parse_mb_per_s\": %.2f,\n", mb_per_s);
  std::printf("  \"mb_definition\": \"10^6 bytes per second, single thread\",\n");
  std::printf("  \"source_sha256\": \"%s\",\n", json_text(sha).c_str());
  std::printf("  \"hardware\": \"%s\",\n", json_text(hardware).c_str());
  std::printf("  \"compiler\": \"%s\"\n", json_text(compiler).c_str());
  std::printf("}\n");
  return 0;
}
