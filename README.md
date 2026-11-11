# framecheck-http

framecheck-http is a C++20 parser for the framing of HTTP/1.1 requests. It finds where one request ends and the next begins, and it rejects any request whose framing is ambiguous instead of guessing. An independent Python oracle, written from a committed spec table, is compared with the parser by a differential runner. A sanitizer fuzzer and a threat model complete the picture. Each threat in the model names a test, and a script checks that the test runs.

To run the fast checks that CI runs, from the project root:

```
make ci
```

This needs a C++20 compiler (clang or gcc), `make`, and Python 3.11 or newer. The tests and the fuzzer use ASan and UBSan, which both clang and gcc support. `make ci` was run on macOS with Apple clang. The Debian g++ run is described in the section Independent review.

## What it parses

The parser reads one request. It returns one of three answers:

- `Complete(consumed, body_length)`: the first request is fully present and valid. `consumed` is where the next request starts.
- `Incomplete`: nothing is broken yet, but the input ends before the request does.
- `Error(class, offset)`: a rule was broken. `class` is one of 16 names such as `cl-te-conflict` or `bad-chunk-ext`, and `offset` is the byte that broke it.

The rules are in [docs/spec-table.md](docs/spec-table.md). Each row has an RFC 9112 section, the expected result, and a row ID that the code refers to. The main rejections are:

- Content-Length together with Transfer-Encoding (FR-01).
- Duplicate Content-Length lines with different values, and a Content-Length that is a comma list (FR-02, FR-03).
- A Transfer-Encoding whose last coding is not `chunked`, or that lists chunked twice (FR-06, FR-07).
- Obsolete line folding, whitespace before the colon, and bare LF or bare CR (HF-01, HF-03, G-01, G-02).
- A chunk size above 2^63 - 1, and a framing field in a trailer (CH-02, CH-08).

There is no lenient mode. The parser does not look past the end of the first request, so the bytes after it are never read.

The spec table covers only the framing rules it lists. The out-of-scope rows (OOS-01 to OOS-08) say what is left out. The parser is not a full RFC 9112 implementation, and it makes no claim about how any real server or proxy behaves.

## Layout

| Path | What is there |
|---|---|
| `include/framecheck/parser.hpp` | The public API: `parse()`, `Result`, `ErrorClass`. |
| `src/` | The parser. `parser.cpp` has the request line, header fields and framing. `chunked.cpp` has chunk sizes, extensions and trailers. |
| `harness/` | The stdin/stdout harness from `docs/harness-protocol.md`, with a small JSON reader. |
| `oracle/` | The Python oracle and its generator. The oracle was written from `docs/spec-table.md` and shares no code with `src/`. |
| `diff/` | The differential runner and its seeded generator. |
| `fuzz/` | The mutational fuzzer, built with ASan and UBSan. |
| `tests/` | Unit tests and the corpus tests. Each test is written `TEST(name)` at the start of a line. |
| `corpus/smuggling/` | 57 requests that must be rejected. |
| `corpus/valid/` | 10 requests that must be accepted. They are the fuzzer's starting seeds. |
| `mutants/` | 17 mutants as patches against `src/`: the review's eight blind mutants (M1 to M8) and nine written later (B1 to B9). |
| `bench/` | The throughput benchmark. |
| `docs/` | The spec table, the harness protocol, the threat model and the disagreement log. |
| `scripts/` | Corpus generation, the threat check, the spec quote check, the line count, the source hash and the mutant runner. |

## Commands

| Command | What it does |
|---|---|
| `make test` | Builds and runs the unit and corpus tests. |
| `make asan-test` | The same tests under ASan and UBSan. It fails on a non-zero exit or on any `runtime error` or `AddressSanitizer` line in the output. |
| `make oracle-test` | The unit tests of the Python oracle and its generator (111 tests). |
| `make diff-fuzz N=200000 SEED=1` | Sends N seeded cases to the harness and to the oracle, and compares them. |
| `make fuzz ITERS=5000000 SEED=1` | Runs the mutational fuzzer under ASan and UBSan. |
| `make bench QUICK=1` | Smoke-mode throughput run. It writes `bench/results.json`. |
| `make threats` | Checks that every test named in `docs/threat-model.md` is run by the unit-test binary. |
| `make loc` | Counts non-test C++ lines with `find`, `wc` and `awk`. |
| `make corpus` | Regenerates `corpus/` from `scripts/make_corpus.py`. |
| `make mutants` | Applies each patch in `mutants/` to a copy of the project and runs the checkers. Several minutes. |
| `make spec-quotes RFC_DIR=DIR` | Checks every quoted phrase in the spec table against `rfc9112.txt` and `rfc9110.txt` in `DIR`. |
| `make ci` | The fast checks: `test`, `asan-test`, `oracle-test`, `threats` and `loc`. |

The differential runner and the corpus generator use the Python named by `PY`, which defaults to `python3`. Both use only the standard library.

## Smuggling corpus

Every file in `corpus/smuggling/` is a request that the spec table says must be rejected. The test `smuggling_corpus_all_rejected_with_close` checks three things for each file: the parser returns `Error`, the harness line for the file contains `"close":true` (row CN-01), and there are at least 40 files. The cases cover Content-Length with Transfer-Encoding, duplicate Content-Length, `xchunked`, chunked followed by identity, a leading space, obsolete line folding, bare LF, chunk framing and framing fields in a trailer.

The corpus proves that the parser rejects each listed shape. The test checks the answer and the close flag, not the class or the offset for each file. It does not prove that no other shape is smuggled, and it does not show how any real proxy or server reacts to these bytes.

## Differential runs

`diff/generate.py` builds requests from random parts. Most are well formed. A few percent of the parts are odd on purpose, and some cases are mutated after assembly. The odd parts include punctuation that is valid in a token (such as `*` in a header name or method), `]` inside a quoted chunk extension, whitespace around `=` in a chunk extension, HTAB in the request target, and chunk sizes at the 2^63 - 1 cap. `diff/run_diff.py` sends the same cases to the harness and to `oracle/reference.py`.

The runner compares the status (accept, reject or incomplete), the consumed bytes and the body length when both sides accept, and the error class, the error offset and the close flag when both sides reject. A `bad-input` line on either side stops the run, because it is a bug in the generator or the runner.

With the same seed, two runs print identical counts. The timing line is the only line that differs.

Review round 2 turned on the class and offset comparison. The first run with it gave 5054 disagreements in 200000 cases. All of them were resolved in the oracle or in the spec table, with the row that decides each one. They are grouped in [docs/disagreements.md](docs/disagreements.md), under Review round 2. The C++ parser was not changed for them.

## Fuzzer

`fuzz/fuzz.cpp` starts from the seed corpus and applies 1 to 4 random edits per input. It checks four things: a complete answer stays the same when only the consumed bytes are given; an error offset lies inside the input; an error in a prefix is the same error in the whole input (by offset and status, see the note in the source); and no parse takes more than one second. ASan and UBSan report the memory and undefined-behaviour bugs they see while the fuzzer runs. The seed fixes the run, so the same seed and corpus repeat the same inputs.

## Threat model

[docs/threat-model.md](docs/threat-model.md) shows a front-end proxy and a back-end server on one connection, and lists 16 STRIDE threats. Twelve name a test, and four are out of scope with a reason. `scripts/check_threats.sh` runs the unit-test binary and fails when a named test is not in its output, or when there are fewer than six threats. A test that is commented out, misspelled or not registered therefore fails the check.

## Benchmark

The benchmark parses a pipelined stream of valid requests on one thread. The full run parses 1 GiB. The spec table says 1 GB and the code uses 1 GiB (2^30 bytes). `QUICK=1` parses 16 MiB and marks its output as a smoke run.

Measured on an Apple M2 with Apple clang 21.0.0, source SHA-256 `4667ef3c595a...`. The machine was shared, with a load average near 6, so read these as approximate. MB means 10^6 bytes.

| Measure | Result | Command |
|---|---|---|
| Parse throughput, 1 GiB pipelined stream of valid requests, one thread | 1,295 and 1,400 MB/s, two runs | `make bench` |
| Fuzz iterations per second under ASan and UBSan | 37,943 | `make fuzz ITERS=5000000` |
| Differential cases per second (harness plus Python oracle) | 18,274 and 17,890, two runs | `make diff-fuzz N=200000 SEED=1` |

The stream holds one small request shape repeated, so the throughput figure is an upper bound for this parser and not a claim about real traffic. Earlier runs of the differential check on a busier machine reached about 9,000 to 11,000 cases per second. Raw data is in `bench/results.json` and `bench/results.run1.json`.

## Line count

`make loc` prints two numbers over the same file set (the non-test C++ under `src/`, `include/`, `harness/` and `fuzz/`):

- `total non-test C++ lines` counts physical lines, blank and comment lines included. This is the metric the amended line-count criterion asks for, because `cloc` is not installed everywhere.
- `code-only lines` leaves out blank lines and lines that are only a `//` comment. This is the closer match to the code column of `cloc`.

The spec asks for 1200 to 2500 lines. The physical count is inside that range. The code-only count is below 1200. The project is not padded to reach the range. Both numbers are reported, and the next section says which one passes.

## Independent review

The reviewer was a separate AI agent that had not seen how the code was written. No person has reviewed this project.

The review, run after the first complete version, planted eight bugs one at a time in copies of the parser (the blind mutants M1 to M8). It also listed problems in the checkers, the documents and the claims. This section records the results of that review and of the fixes that followed. Every number here comes from a run described in this README or in `docs/disagreements.md`.

### Blind mutants

The patches are in `mutants/`. `make mutants` applies each patch to its own copy of the project, then runs `make asan-test`, `make diff-fuzz N=200000 SEED=1` and `make fuzz ITERS=1000000 SEED=1`. A mutant is caught when one of them fails.

- First run, on the project before the fixes in this round: 5 of 8 caught. M1, M4, M6 and M8 were caught by the differential run. M5 was caught only by the fuzzer, because the unit tests copied their input into a `std::string`, whose NUL byte after the end hid the one-byte read. M2, M3 and M7 survived all three checkers.
- After the fixes: 8 of 8 caught. This is a regression result, not a blind one, because the new tests were written with these eight in view.
- A second set of nine mutants, B1 to B9, was written while fixing the review findings. They are not blind either: their author had read the code. All nine are caught.
- `make mutants` on the final code reports 17 of 17 caught. M5 and B9 are memory bugs and the only ones the fuzzer sees. M5 is caught by `asan-test` and `fuzz`, not by the differential run, because an over-read does not change the parse result.

The new tests that catch M2, M3, M5 and M7 are `chunk_ext_quoted_string_allows_right_bracket`, `chunk_ext_whitespace_around_equals_accepted`, `chunk_ext_quoted_string_ends_after_backslash_is_incomplete` and `chunk_ext_junk_after_value_is_bad_chunk_ext`. The exact-size input copy in `tests/test.hpp`, the widened generator and the class and offset comparison in the runner also contribute. M1, M4, M6 and M8 also have new unit tests (`tchar_punctuation_in_field_name_accepted`, `bare_lf_ends_header_section_rejected`, `chunk_ext_empty_value_rejected` and `htab_in_request_target_rejected`).

### Other findings

- The spec table said every row was checked against RFC 9112, and the README said most rows had not been re-read. Every quoted phrase in the table is now matched by `scripts/check_spec_quotes.py`: 33 phrases, all found in the RFC text. Four quotes were not word for word and were corrected. The section numbers were compared with the RFC section headings. The rule wording of each row is a paraphrase and has not been re-read line by line, and the spec table now says so.
- The oracle's unit tests were not run by any target. `make oracle-test` now runs them, and `make ci` and the CI workflow run it.
- The threat check matched the text `TEST(name)`, so a commented-out test passed. It now requires the name in the output of the unit-test binary. A self-test in a copy, which turns `TEST(xchunked_rejected)` into an unregistered function, fails the check.
- The unit tests copy their input into an exact-size heap buffer, so a read one byte past the end is visible under ASan.
- The README overstated what the sanitizers catch. The sentence now says what they report.
- The harness reader kept an earlier string value when a repeated key's last value was not a string. The oracle reports `bad-input` for that input. The reader now drops the key, as the protocol says. This case has unit tests, but it is not in the differential run.
- The chunk-extension rule had two readings for a space before the end of a chunk-size line. The spec table, the oracle and the parser now give one answer, recorded in `docs/disagreements.md`.
- Line count: see the section Line count. Both metrics are printed.
- CI: the Debian g++ run described in `.github/workflows/ci.yml` was run in a `debian:bookworm-slim` container. It passed in a `debian:bookworm-slim` container with g++ 12.2.0 and Python 3.11.2. The run did `make test`, `make asan-test`, `make oracle-test`, `make diff-fuzz N=20000 SEED=1`, `make fuzz ITERS=200000 SEED=1`, `make threats`, `make loc` and `make bench QUICK=1`, and ended with `CI_REHEARSAL_OK`, exit 0. The container was removed when the run ended.

### What is still not covered

- The differential run and the unit tests find disagreements. They cannot find a misreading that the oracle and the parser share. The spec table lists the rows most likely to be misread. Those rows were checked against the RFC text only as far as the quote check and the section numbers go.
- The differential generator makes each kind of odd input at a few percent. An input it never makes is not compared. The fuzzer makes more varied input, but only about 6 percent of its inputs are complete answers, so the complete-answer check runs on fewer cases than its count suggests.
- The mutant set is the eight mutants from the review. It is not a mutation study, and other bugs of the same kinds could survive.
- The smuggling corpus checks Error and the close flag, not the class or the offset for each file.
- The repeated-key case in the harness is covered only by unit tests.
- The benchmark has smoke numbers only. The final run on a quiet machine has not been done.
- The line-count criterion is met on the physical count and not on the code-only count (see Line count).
- Only macOS with Apple clang and one Debian image with g++ 12 were run.

## What is not proven

- That the spec table matches RFC 9112 in every row. The quoted phrases and the section numbers were checked by script. The rule wording of each row is a paraphrase that has not been re-read line by line. The disagreement log lists the decisions that the spec table leaves open.
- That the parser and the oracle do not share a misreading. The differential run cannot find one. The spec table lists the rows most likely to be read the same wrong way.
- That the oracle is independent in any stronger sense than process. The spec asks for a different author of the oracle, written from the table alone. The whole project was produced with an AI coding assistant, and this copy has no git history, so the files cannot show that the oracle was written without reading the parser.
- That the parser is correct for any request outside the generator's reach, or for any input beyond the fuzzer's iteration count.
- That the parser is safe under any compiler, platform or sanitizer configuration other than the ones named above.
- Anything about the behaviour of a real server or proxy. The oracle was written from the spec table, not from observed traffic.
- Performance on any machine other than the one the final benchmark is taken on.
