# Disagreements between the C++ parser and the oracle

Every disagreement that the differential runner reported is listed here with its resolution. A disagreement is resolved against docs/spec-table.md and RFC 9112 text. The side that breaks the spec table is the side that changes. Only a change to the oracle that cites a spec row is allowed as an oracle change.

## Summary

| Run | Command | Cases | Disagreements | Result |
|---|---|---|---|---|
| 1 | `make diff-fuzz N=2000 SEED=1` (generator first draft) | 2000 | 0 | All agree. This draft produced too many request-line errors, so it was rebalanced. |
| 2 | `make diff-fuzz N=20000 SEED=1` (rebalanced generator) | 20000 | 5 | All five are `status`: the C++ parser said incomplete, the oracle said error. Resolved below. The C++ parser was wrong. |
| 3 | `make diff-fuzz N=20000 SEED=1` after the fix | 20000 | 0 | All agree. |
| 4 | `make diff-fuzz N=200000 SEED=1` | 200000 | 0 | All agree. Run twice; the counts are identical. Runs 1 to 4 used the generator before review round 2, so their counts cannot be compared with later runs. |
| 5 | Review round 2: class and offset compared, with the round 2 generator, before the oracle changes below | 200000 | 5054 | 3533 error-class and 1521 error-offset differences. All are resolved in the round 2 section. |
| 6 | Review round 2: after the oracle changes below | 200000 | 0 | All agree on status, consumed, body length, error class and error offset. |

No change was made to `oracle/` during this work. The oracle was run as a black box, and its source was not read.

## Disagreement 1 of 5: request line ends in a CR where a space is required

Run 2 reported five cases. Each input ends in a CR byte (0x0d) at a position where a space is required. The CR is the last byte of every input.

| Case ID | Input hex | Oracle | C++ parser before the fix |
|---|---|---|---|
| g1-0001032 | `504f5354202f612f623f783d31485454502f312e310d` | error, `bad-request-line` at 21 | incomplete |
| g1-0009534 | `44454c455445202f204854540d` | error, `bad-request-line` at 12 | incomplete |
| g1-0010242 | `4d2d53454152430d` | error, `bad-request-line` at 7 | incomplete |
| g1-0012419 | `4d2d534541524348202a5c48543954502f312e310d` | error, `bad-request-line` at 20 | incomplete |
| g1-0013701 | `0d` | error, `bad-request-line` at 0 | incomplete |

Spec rows that decide it:

- Conventions, "Eager syntax": "If a byte cannot belong to a valid message, the parser returns `error` at that byte, even when the input ends before the message does."
- RL-01: `request-line = method SP request-target SP HTTP-version CRLF`, with exactly one SP between the parts.

Decision: the oracle is right. In the first case, the CR at offset 21 follows `HTTP/1.1`, so it is where the request-line grammar needs the end of the line. The request-line grammar needs a CR in that place only before an LF, and a CR is never a space. So the byte at 21 cannot belong to any valid message, whatever comes after it. The eager rule makes this an error at 21, even though the input ends there.

The C++ parser was wrong. Its `unexpected()` helper waited for the byte after a CR so that it could report bare-cr (G-02). That wait is only right where a CRLF is allowed. Here the CR was never allowed, so the parser had to decide at once, and it returned incomplete instead.

Fix: `src/parser.cpp`, `Parser::unexpected()`. The helper no longer reads the byte after the CR. The callers that do allow a CRLF still wait for the LF, through `expect_crlf()` and the header-end check. Regression test: `cr_where_space_is_required_is_error_at_end_of_input` in `tests/test_request_line.cpp`.

Note on the error class. For a CR at the end of the input in a place where it is never valid, the parser now reports `bad-request-line`. If a byte other than LF follows the CR later, the class becomes `bare-cr`. The spec table does not say which class to use here, and the runner compares only status, consumed, body length and close. The fuzzer checks the offset and status across prefixes, and it skips the class check when a prefix ends in CR.

## Review round 2

An independent review found three things. The differential runner did not compare error classes or offsets. The generator did not produce several rule-relevant inputs. The harness reader kept the wrong value for a repeated key. The runner now compares class and offset, and the generator was widened (tchar punctuation in names and methods, `]` and whitespace around `=` in chunk extensions, HTAB in the request target, and sizes at the 2^63 - 1 cap).

The first run with the class and offset comparison, before any change to the oracle, gave 5054 disagreements in 200000 cases. Each one is listed below with the row that decides it. Every change to `oracle/` in this round is a change that cites one of these rows.

| Group | Cases | Oracle said | C++ said | Row that decides it | Side changed |
|---|---|---|---|---|---|
| A | 2340 | `bad-chunk-size` at a space, in `0;a=b c` and similar | `bad-chunk-ext` at the `c` | CH-03 and the eager rule (Conventions). A space that is not BWS before `;` is an error at the next byte, and once an extension has started the class is `bad-chunk-ext`. | Oracle (`read_chunk_extensions`) |
| B | 1123 | `bad-chunk-size` at a space, in `3;a \r\r\n` | `bare-cr` at the CR | Rows G-02 and CH-03. A bare CR is named `bare-cr`, whatever the rule that reached it. | Oracle (`read_chunk_extensions`, through `stray`) |
| C | 1092 | `bad-chunk-size` at a space, in `0 \r\n` | `bad-chunk-size` at the CR | Conventions, eager syntax: the error is at the first byte that cannot belong to a valid line. A space before a CR is allowed only before `;`. | Oracle (`read_chunk_size_line`) |
| D | 429 | `bad-trailer` at the whitespace in `Content-Length : 0` | `bad-trailer` at the start of the line | CH-08: the framing field is rejected at the start of that line, and the name is complete before the colon. | Oracle (`read_field_line` checks CH-08 before HF-01 in a trailer) |
| E | 62 | `bad-content-length` for a list or non-digit value that is also a duplicate | `duplicate-content-length` at the second line | FR-02 comes before FR-03 in the order the spec table gives (the Framing heading says "in this order"). Two values that are not both digits are compared byte for byte. | Oracle (`decide_framing`, `same_length_value`); spec FR-02 now states the comparison |
| F | 8 | `bad-chunk-size` at a space, in `3 \n` | `bare-lf` at the LF | G-01, the same reason as group B. | Oracle, as in group B |

Groups A to F account for all 5054 differences. After the oracle changes, the run with the same inputs gave zero differences (run 6 in the table at the top).

The spec table was changed where it did not decide the case: CH-03 (the byte and the class of a space before the end of a chunk-size line), CH-08 (the order of the name check), FR-02 (the comparison of values that are not digits, and the order of FR-02 and FR-03), and the error-class table (`bad-chunk-size` is now for a space with no extension, and `bad-chunk-ext` for a space after an extension). The C++ parser was not changed for groups A to F. Its answers were the ones the spec table gives, and the table had not said so clearly.

Harness (not part of the differential run): the C++ reader kept an earlier string when a repeated key's last value was not a string. `json.loads` in the oracle keeps the last value, which is not a string, so the oracle reports `bad-input`. The C++ reader now removes the key in that case, as the protocol now says. The test `harness_duplicate_key_last_non_string_drops_the_member` in `tests/test_harness.cpp` covers it. The differential generator does not write repeated keys, so the runner does not cover this case.

Mutant checks. The review's eight blind mutants (M1 to M8) and nine later ones (B1 to B9) are in `mutants/`. Their results are in the README, section Independent review.

## Where the oracle and the C++ parser could still share a misreading

The differential run cannot find a misreading that both sides share. The rows most likely to be read the same wrong way are listed in the "Where a shared misreading would not be caught" section of docs/spec-table.md. They have not been checked against any other source. Two decisions in this implementation are not spelled out in the spec table and are recorded here so that they can be reviewed:

- Duplicate Content-Length lines are compared as decimal numbers, so `5` and `05` count as the same value (FR-02). The spec table says "different decimal values", and this reading follows it. An implementation that compares bytes would reject that pair.
