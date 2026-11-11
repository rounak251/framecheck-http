// Public API of the HTTP/1.1 request-framing parser.
//
// The parser reads one request from a byte buffer and says where it ends.
// It follows docs/spec-table.md. There is no lenient mode: any rule that the
// spec table marks as a rejection is an error here.
#pragma once

#include <cstddef>
#include <cstdint>

namespace framecheck {

// Names match the error classes in docs/spec-table.md. error_class_name()
// returns the exact spelling used in the harness output.
enum class ErrorClass : uint8_t {
  BareLf,
  BareCr,
  BadRequestLine,
  WsBeforeColon,
  BadFieldName,
  ObsFold,
  BadFieldValue,
  ClTeConflict,
  DuplicateContentLength,
  BadContentLength,
  BadTransferEncoding,
  BadChunkSize,
  ChunkSizeOverflow,
  BadChunkExt,
  BadChunkDataTerminator,
  BadTrailer,
};

const char* error_class_name(ErrorClass cls);

struct Result {
  enum class Kind { Complete, Incomplete, Error };

  Kind kind = Kind::Incomplete;
  // Complete only: bytes of the first request, including its final CRLF
  // (or the end of its body or trailer section).
  size_t consumed = 0;
  // Complete only: decoded body octets (Content-Length value, sum of chunk
  // sizes, or 0).
  uint64_t body_length = 0;
  // Error only: the class and the zero-based byte offset of the rule that fired.
  ErrorClass error_class = ErrorClass::BareLf;
  size_t error_offset = 0;
};

// Parses the first request in data[0, size). Bytes after the request are not
// read. Calling again with a longer buffer gives the answer for that buffer.
Result parse(const uint8_t* data, size_t size);

}  // namespace framecheck
