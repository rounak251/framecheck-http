"""Reference oracle for framecheck-http.

This file implements docs/spec-table.md as directly as it can. It does not
share code with the C++ parser and was written from the table alone. Speed
does not matter here. Each check names the row ID it implements, so a reader
can compare a line of code with the row it comes from.

Run as the harness (protocol in docs/harness-protocol.md):

    python3 -I oracle/reference.py < cases.jsonl > results.jsonl
"""

import collections
import json
import re
import sys
from dataclasses import dataclass
from typing import Optional

SP = 0x20
HTAB = 0x09
CR = 0x0D
LF = 0x0A
COLON = ord(":")
SEMI = ord(";")
EQUALS = ord("=")
DQUOTE = ord('"')
BACKSLASH = ord("\\")

# RFC 9110 section 5.6.2. A token is one or more of these bytes.
TCHAR = frozenset(b"!#$%&'*+-.^_`|~0123456789"
                  b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
DIGIT = frozenset(b"0123456789")
HEXDIGIT = frozenset(b"0123456789abcdefABCDEF")

# Spec Conventions: sizes above this are rejected (rows FR-03 and CH-02).
MAX_SIZE = 2**63 - 1

# A field line as read: name in lower case, value with OWS removed, and the
# offset of the line's first byte (used by the framing rows for offsets).
Field = collections.namedtuple("Field", "name value offset")


class NeedMore(Exception):
    """The input ends before the message does. Not an error (row G-03)."""


class Reject(Exception):
    """A byte or rule breaks the spec. Carries the error class and the offset."""

    def __init__(self, error_class, offset):
        super().__init__(error_class, offset)
        self.error_class = error_class
        self.offset = offset


@dataclass
class Result:
    status: str  # "complete", "incomplete" or "error"
    consumed: Optional[int] = None
    body_length: Optional[int] = None
    error_class: Optional[str] = None
    error_offset: Optional[int] = None


class Cursor:
    """A position in the input.

    Every call either returns a byte, raises NeedMore because the input ended,
    or raises Reject at the first byte the grammar forbids. Because reads stop
    at the first forbidden byte, errors are reported eagerly (row G-03).
    """

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def peek(self):
        if self.pos >= len(self.data):
            raise NeedMore()
        return self.data[self.pos]

    def skip(self, count):
        # Bodies are not checked byte by byte, so only their length matters.
        if self.pos + count > len(self.data):
            raise NeedMore()
        self.pos += count

    def take_run(self, allowed):
        """Advance over bytes for which allowed() is true. Return the start.

        The caller decides what the byte that ended the run is allowed to be.
        """
        start = self.pos
        while allowed(self.peek()):
            self.pos += 1
        return start

    def take(self, wanted, error_class):
        if self.peek() != wanted:
            self.stray(error_class)
        self.pos += 1

    def stray(self, error_class):
        """The byte at pos cannot appear here. Report the most specific class.

        A bare LF is row G-01, and a CR that is not followed by LF is row G-02.
        Both win over the caller's class, so the cause is named precisely.
        """
        b = self.data[self.pos]
        if b == LF:
            raise Reject("bare-lf", self.pos)
        if b == CR and self.pos + 1 < len(self.data) and self.data[self.pos + 1] != LF:
            raise Reject("bare-cr", self.pos)
        raise Reject(error_class, self.pos)

    def crlf(self, error_class):
        """Consume the CRLF that ends a line (rows G-01 and G-02)."""
        b = self.peek()
        if b == CR:
            if self.pos + 1 >= len(self.data):
                raise NeedMore()
            if self.data[self.pos + 1] != LF:
                raise Reject("bare-cr", self.pos)
            self.pos += 2
            return
        if b == LF:
            raise Reject("bare-lf", self.pos)
        raise Reject(error_class, self.pos)

    def skip_ows(self):
        while self.peek() in (SP, HTAB):
            self.pos += 1


def take_nonempty(cur, allowed, error_class):
    start = cur.take_run(allowed)
    if cur.pos == start:
        cur.stray(error_class)
    return cur.data[start:cur.pos]


def is_tchar(b):
    return b in TCHAR


def is_visible(b):
    return 0x21 <= b <= 0x7E


def read_request_line(cur):
    """Rows G-04 and RL-01 to RL-04. Return the HTTP minor version, 0 or 1."""
    take_nonempty(cur, is_tchar, "bad-request-line")          # RL-02 method
    cur.take(SP, "bad-request-line")                          # RL-01
    take_nonempty(cur, is_visible, "bad-request-line")        # RL-03 target
    cur.take(SP, "bad-request-line")
    for b in b"HTTP/1.":                                      # RL-04 version
        cur.take(b, "bad-request-line")
    minor = cur.peek()
    if minor not in (ord("0"), ord("1")):
        cur.stray("bad-request-line")
    cur.pos += 1
    cur.crlf("bad-request-line")
    return minor - ord("0")


def read_field_line(cur, trailer):
    """Rows HF-01 to HF-04. In a trailer, every failure is reported as bad-trailer (CH-07)."""

    def tag(error_class):
        return "bad-trailer" if trailer else error_class

    start = cur.pos
    if cur.peek() in (SP, HTAB):                              # HF-03
        raise Reject(tag("obs-fold"), start)
    name = take_nonempty(cur, is_tchar, tag("bad-field-name"))  # HF-02
    name = name.lower()
    if trailer and name in (b"content-length", b"transfer-encoding"):  # CH-08, before HF-01
        raise Reject("bad-trailer", start)
    if cur.peek() in (SP, HTAB):                              # HF-01
        raise Reject(tag("ws-before-colon"), cur.pos)
    cur.take(COLON, tag("bad-field-name"))

    cur.skip_ows()
    value_start = cur.pos
    while True:                                               # HF-04
        b = cur.peek()
        if b in (CR, LF):
            break
        if not (b == HTAB or b == SP or is_visible(b)):
            raise Reject(tag("bad-field-value"), cur.pos)
        cur.pos += 1
    value = cur.data[value_start:cur.pos].rstrip(b" \t")
    cur.crlf(tag("bad-field-value"))
    return Field(name, value, start)


def read_field_section(cur, trailer):
    """Read field lines until the empty line that ends the section (row HF-06)."""
    fields = []
    while True:
        if cur.peek() in (CR, LF):
            cur.crlf("bad-field-name")
            return fields
        fields.append(read_field_line(cur, trailer))


def content_length_value(field):
    """Row FR-03: the value is 1*DIGIT and at most MAX_SIZE."""
    if not field.value or not all(b in DIGIT for b in field.value):
        raise Reject("bad-content-length", field.offset)
    number = int(field.value.decode("ascii"))
    if number > MAX_SIZE:
        raise Reject("bad-content-length", field.offset)
    return number


def check_transfer_codings(field):
    """Rows FR-05, FR-06 and FR-07. The last coding must be chunked, and only once."""
    names = []
    for item in field.value.split(b","):
        item = item.strip(b" \t")
        if not item or not all(is_tchar(b) for b in item):   # FR-05
            raise Reject("bad-transfer-encoding", field.offset)
        names.append(item.lower())
    if names[-1] != b"chunked":                               # FR-06
        raise Reject("bad-transfer-encoding", field.offset)
    if names.count(b"chunked") != 1:                          # FR-07
        raise Reject("bad-transfer-encoding", field.offset)


def same_length_value(a, b):
    """Row FR-02: two values are the same when both are digits and equal as numbers.
    Any other pair is compared byte for byte."""
    if a and b and all(x in DIGIT for x in a) and all(x in DIGIT for x in b):
        return int(a) == int(b)
    return a == b


def decide_framing(fields, minor):
    """Rows FR-01 to FR-11, in the order the spec table lists them.
    Return ("none", 0), ("length", n) or ("chunked", 0)."""
    te = [f for f in fields if f.name == b"transfer-encoding"]
    cl = [f for f in fields if f.name == b"content-length"]

    if te and cl:                                             # FR-01
        raise Reject("cl-te-conflict", max(te[0].offset, cl[0].offset))
    for f in cl[1:]:                                          # FR-02
        if not same_length_value(cl[0].value, f.value):
            raise Reject("duplicate-content-length", f.offset)
    if cl:                                                    # FR-03 (all values are equal now)
        lengths = [content_length_value(cl[0])]
    if len(te) > 1:                                           # FR-04
        raise Reject("bad-transfer-encoding", te[1].offset)

    if te:
        if minor == 0:                                        # FR-09
            raise Reject("bad-transfer-encoding", te[0].offset)
        check_transfer_codings(te[0])
        return ("chunked", 0)
    if cl:                                                    # FR-11
        return ("length", lengths[0])
    return ("none", 0)                                        # FR-10


def is_qdtext(b):
    return b in (HTAB, SP, 0x21) or 0x23 <= b <= 0x5B or 0x5D <= b <= 0x7E


def read_quoted_string(cur):
    """Row CH-03: quoted-string per RFC 9110 section 5.6.4, obs-text excluded."""
    cur.take(DQUOTE, "bad-chunk-ext")
    while True:
        b = cur.peek()
        if b == DQUOTE:
            cur.pos += 1
            return
        if b == BACKSLASH:
            cur.pos += 1
            nxt = cur.peek()
            if not (nxt in (HTAB, SP) or is_visible(nxt)):
                cur.stray("bad-chunk-ext")
            cur.pos += 1
        elif is_qdtext(b):
            cur.pos += 1
        else:
            cur.stray("bad-chunk-ext")


def read_chunk_extensions(cur):
    """Row CH-03: *( BWS ";" BWS name [ BWS "=" BWS ( token / quoted-string ) ] ).
    Called with the cursor on ';'. Stops on the CR or LF that ends the line.

    Whitespace is BWS only when a ';' or the end of the line follows it. Otherwise
    the error is at the first byte after the whitespace, with class bad-chunk-ext,
    because the extension has already started. A bare LF or CR is still named
    bare-lf or bare-cr (rows G-01 and G-02), through stray()."""
    while True:
        cur.pos += 1                                          # the ';'
        cur.skip_ows()
        take_nonempty(cur, is_tchar, "bad-chunk-ext")
        name_end = cur.pos
        cur.skip_ows()
        if cur.peek() == EQUALS:
            cur.pos += 1
            cur.skip_ows()
            if cur.peek() == DQUOTE:
                read_quoted_string(cur)
            else:
                take_nonempty(cur, is_tchar, "bad-chunk-ext")
        else:
            cur.pos = name_end                                # the whitespace is not BWS here
        before_ws = cur.pos
        cur.skip_ows()
        if cur.peek() == SEMI:
            continue
        if cur.pos != before_ws:
            cur.stray("bad-chunk-ext")
        if cur.peek() not in (CR, LF):
            cur.stray("bad-chunk-ext")
        return


def read_chunk_size_line(cur):
    """Rows CH-01, CH-02, CH-03 and CH-04. Return the size; the cursor ends after CRLF."""
    value = 0
    digits = 0
    while cur.peek() in HEXDIGIT:
        value = value * 16 + int(chr(cur.peek()), 16)
        if value > MAX_SIZE:                                  # CH-02
            raise Reject("chunk-size-overflow", cur.pos)
        cur.pos += 1
        digits += 1
    if digits == 0:                                           # CH-01
        cur.stray("bad-chunk-size")
    after_digits = cur.pos
    cur.skip_ows()
    if cur.peek() == SEMI:                                    # CH-03
        read_chunk_extensions(cur)
    elif cur.pos != after_digits:                             # whitespace with no ';' after the size
        cur.stray("bad-chunk-size")
    cur.crlf("bad-chunk-size")
    return value


def read_chunked_body(cur):
    """Rows CH-05, CH-06, CH-07 and CH-09. Return the decoded body length."""
    total = 0
    while True:
        size = read_chunk_size_line(cur)
        if size == 0:                                         # CH-06
            read_field_section(cur, trailer=True)            # CH-07; ends the message
            return total
        cur.skip(size)                                        # CH-05
        cur.crlf("bad-chunk-data-terminator")
        total += size


def parse(data):
    """Parse the first HTTP/1.x request in data. Return a Result (spec sections above)."""
    cur = Cursor(data)
    try:
        minor = read_request_line(cur)
        fields = read_field_section(cur, trailer=False)
        framing, length = decide_framing(fields, minor)
        if framing == "chunked":
            body_length = read_chunked_body(cur)
        elif framing == "length":
            cur.skip(length)
            body_length = length
        else:
            body_length = 0
        return Result("complete", consumed=cur.pos, body_length=body_length)
    except NeedMore:
        return Result("incomplete")
    except Reject as rejection:
        return Result("error", error_class=rejection.error_class,
                      error_offset=rejection.offset)


HEX_TEXT = re.compile(r"(?:[0-9a-fA-F]{2})*")


def respond(line):
    """Turn one input line into the output object of docs/harness-protocol.md."""
    case_id = None
    try:
        case = json.loads(line)
        case_id = case["id"]
        text = case["hex"]
        if not isinstance(case_id, str) or not isinstance(text, str):
            raise TypeError("id and hex must be strings")
        if not HEX_TEXT.fullmatch(text):
            raise ValueError("hex must be an even number of hex digits")
        data = bytes.fromhex(text)
    except (ValueError, KeyError, TypeError):
        return {"body_length": None, "close": False, "consumed": None,
                "error_class": None, "error_offset": None,
                "id": case_id if isinstance(case_id, str) else None,
                "status": "bad-input"}
    result = parse(data)
    return {"body_length": result.body_length,
            "close": result.status == "error",
            "consumed": result.consumed,
            "error_class": result.error_class,
            "error_offset": result.error_offset,
            "id": case_id,
            "status": result.status}


def main():
    for line in sys.stdin:
        if not line.strip():
            continue
        sys.stdout.write(json.dumps(respond(line), sort_keys=True) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
