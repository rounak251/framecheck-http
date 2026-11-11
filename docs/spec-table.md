# Spec table: HTTP/1.1 request framing

This table is the contract between the two independent implementations. The C++ parser and `oracle/reference.py` are both written from this file. Neither one reads the other's source.

## How this table was checked

- RFC 9112 was fetched from `https://www.rfc-editor.org/rfc/rfc9112.txt` on 2026-11-04. Section numbers in the table were compared with its section headings.
- The RFC 9110 grammar for tokens, field values, `Content-Length`, `TE` and `quoted-string` was fetched from `https://www.rfc-editor.org/rfc/rfc9110.txt` on the same day. RFC 9112 refers to RFC 9110 for these rules.
- Every phrase in double quotes in this file was matched against the two RFC texts by `scripts/check_spec_quotes.py`. Each phrase must appear word for word, after whitespace is normalised. The script output is in the README.
- The rule wording in the rows (without quotes) is a paraphrase. It was written from the cited sections. It has not been re-read line by line against the RFC text, so a paraphrase can still be wrong. See the README, section What is not proven.
- This table is not a fixed contract written once before any code. After the first differential runs, review round 2 changed CH-03, CH-08, FR-02, FR-03 and the error-class table so that they state the answers that the C++ parser already gave (docs/disagreements.md, Review round 2). The original text is not recoverable, because the project has no git history. So the claim that the table came first is a claim about process, and this table is weaker evidence of independence than the first version would have been.
- Where RFC 9112 allows a recipient to be lenient and this parser chooses to reject, the row is marked Strict and names the RFC text that allows the leniency.
- Rows that this table does not cover are listed at the end, under Out of scope.

## Conventions

- **Eager syntax.** A byte-level rule is checked on each byte as it arrives. If a byte cannot belong to a valid message, the parser returns `error` at that byte, even when the input ends before the message does. Example: `GET /\x00` with no line ending is an error, not `incomplete`.
- **Framing after the header section.** Rules about how the body is delimited (`FR-*`) need the whole header section. They run once the empty line that ends the header section has been read, in the order listed in FR-01 to FR-09.
- **Incomplete.** The result is `incomplete` only when no rule has been broken by the bytes seen so far and the message is not finished.
- **First message only.** The parser reads one request. Bytes after its end are not an error; they are the next request (G-05).
- **Offsets.** Zero-based byte offset into the input. Each row says which byte is reported.
- **Error classes.** Exactly the names listed in the last section. The differential runner compares status (accept, reject or incomplete), consumed bytes and body length when both sides accept. When both sides reject, it also compares the error class, the error offset and the close flag.
- **Size cap.** Sizes (Content-Length and chunk size) must be at most `2^63 - 1`. The cap is a choice made here. RFC 9112 section 7.1 and RFC 9110 section 8.6 require recipients to anticipate large numerals, but they do not name a number.

## General rules

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| G-01 | CRLF is the only line terminator. A bare LF is rejected. | Section 2.2: "a recipient MAY recognize a single LF as a line terminator". | `error` `bare-lf` at the LF. | Strict. The RFC allows the leniency. |
| G-02 | A CR that is not followed by LF is rejected. | Section 2.2: "A recipient of such a bare CR MUST consider that element to be invalid or replace each bare CR with SP". This parser takes the first option. | `error` `bare-cr` at the CR. | Matches the RFC. |
| G-03 | Byte-level errors are reported eagerly (see Conventions). Framing rules run after the header section. | Section 6.3: the body length depends on the header fields, so framing cannot be decided before they are read. | Depends on the row. | Design choice. Both implementations must follow it, or the same truncated input gets different answers. |
| G-04 | An empty line before the request line is rejected. | Section 2.2: a server "SHOULD ignore at least one empty line (CRLF)" before the request-line. | `error` `bad-request-line` at offset 0. | Strict. The RFC says SHOULD, so ignoring the line is allowed, and this parser does not ignore it. |
| G-05 | The parser reads one request. Trailing bytes are not an error. Its `consumed` value is the length of the first message, so the next request starts there. | Section 9.3.2 (pipelining): a client MAY "pipeline" its requests. Section 6.3 item 7: a request with no framing has length zero. | `complete`; `consumed` is the end of the first message. | None. |

## Request line

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| RL-01 | `request-line = method SP request-target SP HTTP-version CRLF`, with exactly one SP between parts. | Section 3, grammar at line `request-line = method SP request-target SP HTTP-version`. | `error` `bad-request-line` at the first byte that breaks the form. | None. |
| RL-02 | The method is one or more `tchar` characters from RFC 9110 section 5.6.2. It is case-sensitive. There is no length cap. | Section 3.1: "The request method is case-sensitive" and `method = token`. | `error` `bad-request-line`. | None. |
| RL-03 | The request target is one or more visible US-ASCII characters (0x21 to 0x7E). | Section 3.2: "No whitespace is allowed in the request-target". | `error` `bad-request-line`. | The target form (origin, absolute, authority, asterisk) is not checked. See OOS-02. |
| RL-04 | The version must be exactly `HTTP/1.0` or `HTTP/1.1`. The name is case-sensitive. | Section 2.3: `HTTP-name = %s"HTTP"`. Any `HTTP/DIGIT.DIGIT` is syntactically valid. | `error` `bad-request-line` for any other version. | Strict. The RFC allows other versions to be parsed. |

## Header and trailer field lines

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| HF-01 | No SP or HTAB between the field name and the colon. | Section 5.1: "A server MUST reject, with a response status code of 400 (Bad Request), any received request message that contains whitespace between a header field name and colon." | `error` `ws-before-colon` at the SP or HTAB. | Required by the RFC. |
| HF-02 | The field name is one or more `tchar` (RFC 9110 section 5.6.2). An empty name, a non-tchar byte, or a line with no colon is rejected. | Section 5.1, `field-line = field-name ":" OWS field-value OWS`. | `error` `bad-field-name` at the first byte that breaks the form. If the name is empty, at the name start. | None. |
| HF-03 | A line that starts with SP or HTAB is rejected. This covers folded continuation lines and a whitespace-led line directly after the request line. | Section 5.2: a server that receives an obs-fold in a request "MUST either reject the message by sending a 400 (Bad Request)". Section 2.2: whitespace before the first field "MUST either reject the message as invalid or consume each whitespace-preceded line". | `error` `obs-fold` at the SP or HTAB. | Strict. The parser takes the reject option, not the replace or consume option. |
| HF-04 | The field value is trimmed of leading and trailing SP and HTAB. The rest must be HTAB, SP or 0x21 to 0x7E. Any other byte is rejected, including 0x7F, other control bytes and obs-text (0x80 to 0xFF). | Section 5.1: "OWS occurring before the first non-whitespace octet of the field line value" is excluded by parsers. RFC 9110 section 5.5 defines `field-vchar = VCHAR / obs-text`. | `error` `bad-field-value` at the first bad byte. | Strict. RFC 9110 section 5.5 allows obs-text, and this parser rejects it. |
| HF-05 | An empty field value is accepted. | RFC 9110 section 5.5: `field-value = *field-content`. | Accepted. | None. |
| HF-06 | The header section ends at the first empty line (a CRLF with nothing before it). | Section 2.2: "read each header field line into a hash table by field name until the empty line". | Not an error. | None. |

## Framing decisions (run after HF-06, in this order)

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| FR-01 | A message with both `Transfer-Encoding` and `Content-Length` is rejected. Offset is the later of the first TE line and the first CL line. | Section 6.3 item 3: such a message "ought to be handled as an error". Section 6.1: a server "MAY reject" it, and "MUST close the connection". Section 11.2 covers smuggling. | `error` `cl-te-conflict`. | Strict. The RFC also allows processing by TE alone. |
| FR-02 | Two or more `Content-Length` lines with different values are rejected. Offset is the first CL line that differs from the first one. Two values are the same when both are all digits and equal as numbers (so `5` and `05` are the same). Any other pair is compared byte for byte. Identical values are accepted and the shared value is the body length. FR-02 is checked before FR-03. | Section 6.3 item 5: a Content-Length that is not one valid value "MUST treat it as an unrecoverable error", unless the values form a comma-separated list whose values are all valid and all the same, in which case "the message is processed with that single value". RFC 9110 section 8.6 says a recipient "MAY either reject the message as invalid or replace" a list of identical values, such as `42, 42`. | `error` `duplicate-content-length`. | Accepting identical duplicate lines is the leniency that section 6.3 item 5 allows. |
| FR-03 | A `Content-Length` value must be `1*DIGIT` and at most `2^63 - 1`. The value is decimal, with no sign, no internal space and no comma list. Offset is the start of the line. The value checked is the first one, because FR-02 has made all of them equal. | RFC 9110 section 8.6: `Content-Length = 1*DIGIT`. Section 6.3 item 5: an invalid value means the framing is invalid, and the recipient "MUST treat it as an unrecoverable error". | `error` `bad-content-length`. | Strict. Section 6.3 item 5 allows a comma list of identical values (for example `42, 42`), and this parser rejects it. |
| FR-04 | Two or more `Transfer-Encoding` lines are rejected. Offset is the second TE line. | RFC 9110 section 5.3 allows combining same-name lines into one list. | `error` `bad-transfer-encoding`. | Strict. The combining is allowed, not required. |
| FR-05 | The TE value is a comma-separated list of transfer-coding tokens. OWS around each comma is allowed. Empty elements and parameters (anything with `;`) are rejected. Offset is the TE line start. | Section 6.1 grammar `Transfer-Encoding = #transfer-coding`. RFC 9110 section 5.6.1.2 says recipients "MUST accept" empty list elements. Section 7.1: chunked "does not define any parameters. Their presence SHOULD be treated as an error." | `error` `bad-transfer-encoding`. | Strict. The RFC requires the parser to accept empty elements, and this parser rejects them. |
| FR-06 | The last coding must be `chunked`, compared case-insensitively. Examples that are rejected: `gzip` alone, `chunked, identity`, `xchunked`. | Section 6.3 item 4: for a request, a TE whose final coding is not chunked means "the server MUST respond with the 400 (Bad Request) status code and then close the connection". Section 6.1: the sender "MUST apply chunked as the final transfer coding". Section 7: "All transfer-coding names are case-insensitive". | `error` `bad-transfer-encoding` at the TE line start. | None. |
| FR-07 | `chunked` may appear only once in the list. | Section 6.1: "A sender MUST NOT apply the chunked transfer coding more than once". | `error` `bad-transfer-encoding` at the TE line start. | Strict. The RFC says it is a sender rule, and this parser also rejects it on receipt. |
| FR-08 | Codings before the final `chunked` are accepted for framing (for example `gzip, chunked`). The body bytes are not decoded. | Section 6.1: a recipient "MAY decode the received transfer coding(s)". | Accepted. Body length counts the chunked-decoded octets, not the content after any other coding. | The 501 response of section 6.1 is a server response, and this table does not model responses. |
| FR-09 | A request with HTTP/1.0 and a `Transfer-Encoding` header is rejected. Offset is the TE line start. | Section 6.1, last paragraphs: the recipient "MUST treat the message as if the framing is faulty". | `error` `bad-transfer-encoding`. | None. |
| FR-10 | Without `Transfer-Encoding` or `Content-Length`, the body length is 0 and `consumed` is the end of the header section. | Section 6.3 item 7: "the message body length is zero". | Accepted. | None. |
| FR-11 | With only a valid `Content-Length` of n, the body is exactly n octets. If fewer arrive, the result is `incomplete`. `consumed` is the header end plus n. | Section 6.3 item 6. | Accepted or `incomplete`. | None. |

## Chunked transfer coding

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| CH-01 | `chunk-size = 1*HEXDIG`. Hex digits may be upper or lower case, and leading zeros are allowed. | Section 7.1: `chunk-size = 1*HEXDIG`. | Accepted. A line with no digits is `error` `bad-chunk-size`. | None. |
| CH-02 | Each digit is checked as it arrives. If the value passes `2^63 - 1`, the result is `error` `chunk-size-overflow` at that digit. | Section 7.1: "recipients MUST anticipate potentially large hexadecimal numerals and prevent parsing errors due to integer conversion overflows". | `error` `chunk-size-overflow`. | The cap is a choice made here. See Conventions. |
| CH-03 | Chunk extensions follow `chunk-ext = *( BWS ";" BWS chunk-ext-name [ BWS "=" BWS chunk-ext-val ] )`. The value is a token or a quoted-string. Unknown extensions are ignored. Whitespace is BWS only when a `;` follows it. Otherwise the error is at the first byte after the whitespace. Before any `;`, that error is `bad-chunk-size`. After a `;`, it is `bad-chunk-ext`. An empty value after `=` is `bad-chunk-ext` at the byte where the value should start. | Section 7.1.1: "A recipient MUST ignore unrecognized chunk extensions." | Malformed extension: `error` `bad-chunk-ext`. Whitespace after the size with no extension and no `;`: `error` `bad-chunk-size`. | Strict. Inside a quoted-string this parser rejects obs-text (0x80 to 0xFF) and any quoted-pair whose next byte is not HTAB, SP or VCHAR. A bare LF or CR is still `bare-lf` or `bare-cr` (G-01, G-02). |
| CH-04 | The chunk-size line ends with CRLF. Bare LF and bare CR are rejected under G-01 and G-02. | Section 7.1 grammar: `chunk = chunk-size [ chunk-ext ] CRLF`. | `bare-lf` or `bare-cr`. | None. |
| CH-05 | Chunk data is exactly chunk-size octets. It may contain any byte values, including CR and LF. It is followed by CRLF. If the CRLF is missing, the result is `error` `bad-chunk-data-terminator` at the byte where CRLF was expected. | Section 7.1 grammar: `chunk-data = 1*OCTET` and `chunk = chunk-size [ chunk-ext ] CRLF chunk-data CRLF`. | `error` `bad-chunk-data-terminator`. | None. |
| CH-06 | A chunk size of zero starts `last-chunk`. Any number of zero digits is allowed, and an extension is allowed. | Section 7.1: `last-chunk = 1*("0") [ chunk-ext ] CRLF`. | Accepted. | None. |
| CH-07 | The trailer section is zero or more field lines, each ending in CRLF, followed by an empty line. Every rule in HF-01 to HF-04 applies. Any violation is reported as `bad-trailer`, at the same byte. | Section 7.1.2: `trailer-section = *( field-line CRLF )`. The empty line after it is the final CRLF of `chunked-body`. | `error` `bad-trailer`. | None. |
| CH-08 | A trailer named `Content-Length` or `Transfer-Encoding` is rejected at the start of that line. The name is checked as soon as it is complete, before the colon and before any whitespace after the name, so `Content-Length : 0` is rejected at the line start. | Section 7.1.2 points to section 6.5 of RFC 9110. RFC 9110 section 6.5.1 says that fields needed for framing "cannot be processed outside the header section". Section 7.1.2 lets a recipient "selectively retain or discard the received trailer fields". | `error` `bad-trailer`. | Strict. The RFC does not require a recipient to reject these trailers. The rule is a choice made here, because a framing field in a trailer is a smuggling vector. |
| CH-09 | The body length is the sum of all chunk sizes. `consumed` is the byte after the final CRLF of the trailer section. | Section 7.1.3, pseudo-code: `Content-Length := length`. Section 6.3 item 4. | Accepted. | None. |

## Connection handling

| ID | Rule | RFC 9112 check | Result on violation | Strictness note |
|---|---|---|---|---|
| CN-01 | Every `error` result sets `close` to true in the harness output. | Section 6.3 item 5: for a request, the server "MUST respond with a 400 (Bad Request) status code and then close the connection". Section 6.1 says the same for CL with TE. Section 9.6 covers tear-down. | Harness field `close` = true. | Design choice. After a framing error the next message boundary is unknown, so reusing the connection is unsafe. |

## Out of scope

These rules exist in RFC 9112 but this parser does not implement them. Each one is listed so that nobody reads the table as full RFC 9112 compliance.

| ID | Rule | RFC 9112 section | Why out of scope |
|---|---|---|---|
| OOS-01 | Host header required for HTTP/1.1 requests. | Section 3.2 | It is not a framing rule. |
| OOS-02 | Checks of the request-target form (origin, absolute, authority, asterisk). | Section 3.2 | RL-03 only checks bytes. |
| OOS-03 | Decoding of content codings or transfer codings other than framing. | Sections 6.1, 7.2 | The body is never decoded. |
| OOS-04 | CONNECT and Upgrade semantics, including tunnels. | Sections 6.3 item 2, 9.3 | Only the framing of the request is modelled. |
| OOS-05 | Limits on header count, header size and chunk-extension total length. | Section 7.1.1 ("ought to limit the total length of chunk extensions") | This parser has no such limit, so a long input is only bounded by the input length. |
| OOS-06 | Response framing: HEAD, 1xx, 204, 304 and close-delimited bodies. | Section 6.3 items 1, 8 | Requests only. |
| OOS-07 | HTTP/2, HTTP/3, TLS and network behaviour. | Not in RFC 9112 | Out of the project. |
| OOS-08 | Behaviour of any real server or proxy. | Not applicable | The oracle is written from this table, not from observed traffic. |

## Error classes

Every class name below is used exactly as written.

| Class | Used by |
|---|---|
| `bare-lf` | G-01, CH-04 |
| `bare-cr` | G-02, CH-04 |
| `bad-request-line` | G-04, RL-01 to RL-04 |
| `ws-before-colon` | HF-01 |
| `bad-field-name` | HF-02 |
| `obs-fold` | HF-03 |
| `bad-field-value` | HF-04 |
| `cl-te-conflict` | FR-01 |
| `duplicate-content-length` | FR-02 |
| `bad-content-length` | FR-03 |
| `bad-transfer-encoding` | FR-04 to FR-07, FR-09 |
| `bad-chunk-size` | CH-01, CH-03 (whitespace after the size, with no extension) |
| `chunk-size-overflow` | CH-02 |
| `bad-chunk-ext` | CH-03 (malformed extension, including whitespace before the CRLF after one) |
| `bad-chunk-data-terminator` | CH-05 |
| `bad-trailer` | CH-07, CH-08 |

## Where a shared misreading would not be caught

The differential runner finds places where the two implementations disagree. It cannot find a place where both implementations read the table the same wrong way. These are the rows where that is most likely, so they are the first to check against the RFC text:

- FR-02 and FR-03: identical duplicate Content-Length lines are accepted, while a comma list of identical values is rejected. Both are easy to read the other way. The spec text follows section 6.3 item 5 for the duplicate lines, which the RFC treats as one combined value.
- FR-05 and FR-08: which codings are allowed before a final `chunked`, and whether unknown codings are only framed or must be decoded.
- CH-03: whether a space before the CRLF is an extension error or a size error. This table gives the size error when no extension has started and the extension error after one. The rule was settled in review round 2 (docs/disagreements.md).
- CH-08: whether framing fields in a trailer are rejected or ignored. This table rejects them, which the RFC allows but does not require.
- G-03: whether a truncated input with a bad byte is `error` or `incomplete`. This table says `error`.

A misreading shared by both sides can only be found by checking the row against the RFC text again, or against an RFC test suite if one exists.
