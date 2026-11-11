# Harness protocol

Both the C++ harness and `oracle/reference.py` read cases from stdin and write results to stdout. The differential runner sends the same input to both and compares the answers. This file defines the format so that the two programs can be written independently.

## Transport

- Input: one JSON object per line, UTF-8, each line ended by `\n`.
- Output: exactly one JSON object per non-blank input line, in the same order, each ended by `\n`. Flush stdout after every line.
- Blank input lines (empty or whitespace only) produce no output.
- Each JSON object is written with its keys in alphabetical order. The runner parses the JSON, so the order is for reading the output by hand, not for matching.
- The process exits with status 0 after end of input. Bad input lines do not stop the process (see below).

## Input object

```json
{"id": "g1-0000042", "hex": "474554202f20485454502f312e310d0a0d0a"}
```

| Field | Type | Meaning |
|---|---|---|
| `id` | string | Case name, unique within one run. The output echoes it. |
| `hex` | string | The raw input bytes, written as hexadecimal. |

Why hex: the cases contain NUL, CR, LF and bytes above 0x7F, which are not safe to put inside a JSON string. Hex carries every byte exactly.

Rules for `hex`:

- Only the characters `0-9`, `a-f` and `A-F`. No spaces, no `0x` prefix, no separators.
- An even number of characters.
- The empty string means zero bytes.

Anything else is a bad input (below).

If a key appears more than once in the input object, the last value counts, as in JSON. A last value that is not a string removes the key, so a `hex` key whose last value is a number is a missing `hex`, which is a bad input.

## Output object

```json
{"body_length":5,"close":false,"consumed":42,"error_class":null,"error_offset":null,"id":"g1-0000042","status":"complete"}
```

| Field | Type | Meaning |
|---|---|---|
| `id` | string or null | Echo of the input `id`. Null if the input had no usable `id`. |
| `status` | string | `complete`, `incomplete`, `error` or `bad-input`. |
| `consumed` | integer or null | For `complete`: bytes of the first message, including its final CRLF. Null otherwise. |
| `body_length` | integer or null | For `complete`: decoded body octets (Content-Length value, or the sum of chunk sizes, or 0). Null otherwise. |
| `error_class` | string or null | For `error`: one of the classes in `docs/spec-table.md`. Null otherwise. |
| `error_offset` | integer or null | For `error`: zero-based byte offset, as defined by the row that fired. Null otherwise. |
| `close` | boolean | True if and only if `status` is `error` (row CN-01). |

### Meaning of each status

- `complete`: the first message is fully present and valid. Any bytes after `consumed` are the next message and are not reported.
- `incomplete`: no rule has been broken, and the input ends before the message does. The caller should read more bytes and ask again. The harness never sends more bytes in one case, so `incomplete` is a valid final answer for a truncated case.
- `error`: a rule has been broken. The first byte that breaks a rule decides the offset. The connection must be closed (`close` is true).
- `bad-input`: the input line could not be used. This is a bug in the case generator or in the runner. The differential runner counts these separately and never treats them as agreement.

### Bad input

If a line is not a JSON object, has no string `id`, or has a `hex` field that is missing or invalid, the harness writes one line:

```json
{"body_length":null,"close":false,"consumed":null,"error_class":null,"error_offset":null,"id":null,"status":"bad-input"}
```

`id` is set to the input `id` when it is a string, and to null otherwise. The harness then continues with the next line.

## Comparison rules for the runner

The runner compares the two outputs for each case on:

1. `status`, where `error` counts as "reject" and `complete` counts as "accept". `incomplete` must match `incomplete` exactly.
2. `consumed` and `body_length`, when both say `complete`.
3. `error_class` and `error_offset`, when both say `error`. A difference in either is a disagreement.
4. `close`, when both say `error`. Both must be true, which is already required by the output rules.

The class and the offset were not compared before review round 2. They are compared now, because a class-only mistake in either side (for example, reporting `bad-chunk-size` where the spec says `bad-chunk-ext`) changes the answer that a caller sees. How each difference that was found was resolved is in `docs/disagreements.md`.

Any `bad-input` on either side is a runner error and stops the run.

## Examples

Valid request, body of 5 octets, pipelined second request after it:

input
```
{"id":"ex-1","hex":"504f5354202f20485454502f312e310d0a436f6e74656e742d4c656e6774683a20350d0a0d0a68656c6c6f474554202f"}
```
output
```
{"body_length":5,"close":false,"consumed":43,"error_class":null,"error_offset":null,"id":"ex-1","status":"complete"}
```

Content-Length and Transfer-Encoding together (row FR-01):

input
```
{"id":"ex-2","hex":"504f5354202f20485454502f312e310d0a436f6e74656e742d4c656e6774683a20350d0a5472616e736665722d456e636f64696e673a206368756e6b65640d0a0d0a"}
```
output
```
{"body_length":null,"close":true,"consumed":null,"error_class":"cl-te-conflict","error_offset":36,"id":"ex-2","status":"error"}
```

Bare LF in the request line (row G-01):

input
```
{"id":"ex-3","hex":"474554202f20485454502f312e310a"}
```
output
```
{"body_length":null,"close":true,"consumed":null,"error_class":"bare-lf","error_offset":14,"id":"ex-3","status":"error"}
```

The outputs above are what `oracle/reference.py` printed for these inputs on 2026-11-04. The unit tests in `oracle/test_reference.py` check the same behaviour.

## Versions

This protocol is version 1. A change to any field name, status name or error class is a change of protocol and must be recorded in this file and in `docs/disagreements.md` before either side is changed.
