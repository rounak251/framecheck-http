# Threat model: request framing between a front-end proxy and a back-end server

This model covers one connection, where a front-end proxy forwards requests to a back-end server on the same keep-alive connection. The question is where each side thinks one request ends and the next one starts. If the two sides disagree, the bytes that the back end reads as a second request were sent by the client as part of the first one.

The model does not describe any real proxy or server. The mitigations are what this parser does. The parser is the framing check that the front end would run before it forwards a request.

## Data flow

```
                 untrusted bytes
  client ----------------------------------> [F] front-end proxy
                                              |  framing check: framecheck parse()
                                              |  Complete  -> forward the consumed bytes
                                              |  Error     -> send 400, close connection (CN-01)
                                              |  Incomplete-> read more bytes
                                              v
                                       one TCP connection, bytes only
                                              |
                                              v
  [B] back-end server  <------------------ forwarded bytes
        its own request framing
        must agree with [F] on where each request ends
```

Trust boundaries:

- The client is untrusted. Every byte that reaches `parse()` came from the client.
- The line between [F] and [B] is the trust boundary for framing. Each side applies its own rules, so any rule that one side applies and the other does not is a way to smuggle a request.
- The parser is in the process of [F]. It runs on bytes and returns a decision. It does not open sockets.

## Threats

STRIDE categories are given for each row. The Test column names an existing `TEST(name)` in `tests/`. `scripts/check_threats.sh` fails if a name in that column is missing from the test suite. A row marked "out of scope" has no test, and the reason is given in the Mitigation column.

| ID | STRIDE | Threat | Mitigation | Test |
|---|---|---|---|---|
| T-01 | Tampering | Client sends both Content-Length and Transfer-Encoding, so [F] and [B] pick different framings. | FR-01 rejects the message with a close. | `cl_and_te_together_rejected` |
| T-02 | Tampering | Client sends two Content-Length lines with different values, and one side uses the first and the other the last. | FR-02 rejects the message. Identical lines are accepted only when they name the same number. | `duplicate_cl_different_values_rejected` |
| T-03 | Tampering | Client hides chunked behind a coding name that one side does not know, such as `xchunked`, or puts a coding after chunked. | FR-06 requires chunked as the last coding and compares it exactly, ignoring case. | `xchunked_rejected` |
| T-04 | Tampering | Client sends an obs-fold continuation line, so one side joins it to the previous header and the other reads it as a new header. | HF-03 rejects any line that starts with SP or HTAB. | `obs_fold_rejected` |
| T-05 | Tampering | Client uses a bare LF as a line end. One side ends the line there and the other waits for CRLF. | G-01 rejects a bare LF. The parser never treats LF alone as a line end. | `bare_lf_in_header_rejected` |
| T-06 | Tampering | Client puts whitespace between a field name and its colon, so one side sees a different header name. | HF-01 rejects the message. | `whitespace_before_colon_rejected` |
| T-07 | Tampering | Client places a framing field in the trailer of a chunked body, where a later parser might read it as a header. | CH-08 rejects Content-Length and Transfer-Encoding in a trailer, at the start of the line. | `trailer_content_length_rejected` |
| T-08 | Denial of service | Client sends a chunk size with more digits than fit in 64 bits, hoping for a wrap-around to a small size. | CH-02 checks each digit as it arrives and rejects a value above 2^63 - 1 at that digit. | `chunk_size_overflow_rejected` |
| T-09 | Tampering | Front end waits for more bytes on a request that is already malformed. The back end gets a different answer for the same bytes. | Eager syntax (spec table, Conventions). A byte that cannot belong to a valid message is an error at once. Found by the differential run, see docs/disagreements.md. | `cr_where_space_is_required_is_error_at_end_of_input` |
| T-10 | Repudiation | A connection keeps being used after a rejected request. The next bytes are read with no known start. | CN-01: every error sets close to true in the harness output. The smuggling corpus checks this for each case. | `smuggling_corpus_all_rejected_with_close` |
| T-11 | Information disclosure | Bytes after the first request are read as part of it, so the back end gets the wrong request boundary. | G-05: the parser never reads past `consumed`. | `bytes_after_request_are_not_read` |
| T-12 | Denial of service | A very long run of hexadecimal digits makes the parser read past the end of the input or overflow its integer. | Bounds checks on every read, and the digit-by-digit cap in CH-02. Both are exercised under ASan and UBSan by `make asan-test` and `make fuzz`. | `chunk_size_very_long_run_of_digits_overflow_not_crash` |
| T-13 | Spoofing | A client claims to be a trusted proxy by adding a header such as X-Forwarded-For. | Out of scope. The parser does not interpret any header except the framing fields. Identity rules belong to the back end. | out of scope |
| T-14 | Denial of service | A request with a huge number of headers, or very long chunk extensions, uses a lot of time or memory. | Out of scope. OOS-05 in the spec table. The parser has no limit on these, so the cost grows with the input length. | out of scope |
| T-15 | Tampering | A client uses HTTP/2 or a TLS-level trick to reach the back end without going through the framing check. | Out of scope. OOS-07 in the spec table. Only HTTP/1.1 request framing is modelled. | out of scope |
| T-16 | Repudiation | Nobody can later show which bytes were rejected and when. | Out of scope. Logging is not part of this project. | out of scope |

## What the mitigations do not cover

- The parser decides whether one request is well framed. It does not check what the back end does with the body. A request that is framed correctly can still carry an attack in its body or headers.
- Both sides of a real connection have their own bugs. This model assumes [B] applies the rules in the spec table. It does not test [B].
- The rules are a subset of RFC 9112 (see the Out of scope section of the spec table). A rule that is not in the table is not enforced.
- Shared misreadings of the spec table would be shared by the parser and the oracle. The differential run cannot find them. docs/spec-table.md lists the rows most likely to be read the same wrong way.
