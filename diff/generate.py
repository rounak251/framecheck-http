"""Seeded generator and mutator of HTTP/1.1 request bytes for the differential runner.

A case is raw bytes. Most cases are a random request with a random set of
framing headers. Some are built to break one framing rule, some have their
bytes changed after assembly, and some have a second request appended. The
generator does not decide whether a case is valid. The C++ parser and the
oracle decide that, and the runner compares them.
"""

import random

SP = b" "
CRLF = b"\r\n"

# Most requests are well formed. The odd pieces below are used at a few percent,
# so that the framing rules get enough coverage and the request line does not
# reject almost every case.
METHODS = [b"GET", b"POST", b"PUT", b"DELETE", b"PATCH", b"get", b"M-SEARCH", b"G*T", b"X!#$%&'+-.^_`|~"]
METHODS_ODD = [b"P\x00ST", b"", b"G E"]
TARGETS = [b"/", b"/a/b?x=1", b"*", b"/%zz"]
TARGETS_ODD = [b"/\x7f", b"", b"/a b", b"/a\tb"]
VERSIONS = [b"HTTP/1.1"] * 6 + [b"HTTP/1.0"]
VERSIONS_ODD = [b"HTTP/1.2", b"http/1.1", b"HTTP/2.0", b"HTTP/1.10"]
NAMES = [b"Host", b"Accept", b"X-Test", b"user-agent", b"X_Under", b"X.Dot", b"X!Bang",
         b"X*Y", b"X~|`^'+#$%&"]
ODD_NAMES = [b"Bad/Name", b"", b"Sp ace", b"X\x01", b"X\xe9", b"X\x00Y"]
VALUES = [b"example.com", b"*/*", b"", b"a b  c", b"\t tab", b'q=1;v="2"']
VALUES_ODD = [b"x\x7fy", b"caf\xe9"]
ODD_RATE = 0.04


def pick(rng, good, odd):
    """Usually a good item, sometimes an odd one."""
    return rng.choice(odd) if rng.random() < ODD_RATE else rng.choice(good)

TE_VALUES = [
    b"chunked", b"Chunked", b"gzip, chunked", b"chunked, gzip", b"xchunked", b"chunked, chunked",
    b"gzip,,chunked", b"chunked;q=1", b" chunked , gzip", b"chunked,", b",chunked", b"identity",
    b"gzip , chunked", b"chunkedx", b"", b"chunked\xe9", b"compress, deflate, chunked",
]
CL_BAD = [b"+5", b"5, 5", b"0x5", b"5a", b"", b"9" * 20, b"-1", b"5 6", b"  ", b"\x005"]
TRAILERS = [b"X-T: v", b"Content-Length: 3", b"Transfer-Encoding: chunked", b"Bad Name: x",
            b"X: a", b" b", b"X-Empty:", b"Content-Length : 0"]
CHUNK_EXTS = [b"", b";a", b";a=b", b';a="x y"', b" ;a=1", b";a=", b";=x", b"; a", b';a="\\"q"',
              b';a="\xe9"', b" ", b" ;", b";a \x0d", b";a;b=c;d", b"\t;e",
              b';a="]]"', b";a = b", b";a=b\x01", b';a="\\]"', b";a=b c", b';a=""']
# Sizes at and past the 2^63 - 1 cap (CH-02). The answer for these is an error
# at the digit that passes the cap, or incomplete, depending on the length.
CHUNK_SIZES_CAP = [b"7fffffffffffffff", b"8000000000000000", b"ffffffffffffffff",
                   b"07fffffffffffffff", b"0000000000000000"]
INTERESTING = [0x00, 0x09, 0x0A, 0x0D, 0x20, 0x3A, 0x2C, 0x3B, 0x22, 0x5C, 0x80, 0xFF, 0x7F,
               0x30, 0x39, 0x41, 0x61, 0x66]


class Builder:
    def __init__(self, rng):
        self.rng = rng

    def eol(self):
        roll = self.rng.random()
        if roll < 0.99:
            return CRLF
        if roll < 0.995:
            return b"\n"
        return b"\r"

    def printable(self, n):
        return bytes(self.rng.choice(b"abcdefXYZ 0123456789:;,=\"/") for _ in range(n))

    def random_bytes(self, n):
        return bytes(self.rng.randrange(256) for _ in range(n))

    def request_line(self):
        rng = self.rng
        method = pick(rng, METHODS, METHODS_ODD)
        target = pick(rng, TARGETS, TARGETS_ODD)
        version = pick(rng, VERSIONS, VERSIONS_ODD)
        sep1 = SP * (2 if rng.random() < ODD_RATE / 4 else 1)
        return method + sep1 + target + SP + version + self.eol()

    def header_line(self):
        rng = self.rng
        roll = rng.random()
        name = pick(rng, NAMES, ODD_NAMES)
        value = pick(rng, VALUES, VALUES_ODD)
        colon = b":"
        if rng.random() < ODD_RATE / 4:
            colon = rng.choice([b" :", b"\t:", b":\x00"])
        pad = SP * rng.randrange(0, 3)
        line = name + colon + pad + value + self.eol()
        if rng.random() < ODD_RATE / 4:
            line = SP + line  # obs-fold or a leading space
        return line

    def framing_lines(self):
        """Returns (header lines, body kind, body length) for one random framing scenario."""
        rng = self.rng
        scenario = rng.choice([
            "none", "none", "cl", "cl", "cl", "cl-dup-same", "cl-dup-differ", "cl-dup-zeros",
            "cl-bad", "te", "te", "te", "te-dup", "cl-te", "te-odd", "cl-te",
        ])
        if scenario == "none":
            return [], "none", 0
        if scenario == "cl":
            n = rng.randrange(0, 12)
            return [b"Content-Length: " + str(n).encode() + self.eol()], "cl", n
        if scenario == "cl-dup-same":
            n = rng.randrange(0, 8)
            v = str(n).encode()
            return [b"Content-Length: " + v + self.eol(), b"Content-Length: " + v + self.eol()], "cl", n
        if scenario == "cl-dup-zeros":
            n = rng.randrange(1, 8)
            return [b"Content-Length: " + str(n).encode() + self.eol(),
                    b"Content-Length: 0" + str(n).encode() + self.eol()], "cl", n
        if scenario == "cl-dup-differ":
            n = rng.randrange(0, 8)
            return [b"Content-Length: " + str(n).encode() + self.eol(),
                    b"Content-Length: " + str(n + 1).encode() + self.eol()], "cl", n
        if scenario == "cl-bad":
            return [b"Content-Length: " + rng.choice(CL_BAD) + self.eol()], "none", 0
        if scenario == "te":
            return [b"Transfer-Encoding: " + rng.choice(TE_VALUES[:4]) + self.eol()], "chunked", 0
        if scenario == "te-odd":
            return [b"Transfer-Encoding: " + rng.choice(TE_VALUES) + self.eol()], "chunked", 0
        if scenario == "te-dup":
            return [b"Transfer-Encoding: chunked" + self.eol(),
                    b"Transfer-Encoding: " + rng.choice(TE_VALUES) + self.eol()], "chunked", 0
        # cl-te: both framing headers in one request
        n = rng.randrange(0, 6)
        return [b"Content-Length: " + str(n).encode() + self.eol(),
                b"Transfer-Encoding: chunked" + self.eol()], "chunked", 0

    def chunked_body(self):
        rng = self.rng
        out = []
        for _ in range(rng.randrange(0, 4)):
            n = rng.randrange(0, 12)
            data = self.printable(n) if rng.random() < 0.8 else self.random_bytes(n)
            text = format(len(data), "x")
            if rng.random() < 0.02:
                text = rng.choice(CHUNK_SIZES_CAP).decode("ascii")
            if rng.random() < 0.3:
                text = text.upper()
            if rng.random() < 0.1:
                text = "000" + text
            if rng.random() < 0.05:
                data = data + b"x"  # wrong size on purpose: the terminator check must fire
            out.append(text.encode() + rng.choice(CHUNK_EXTS) + self.eol() + data + self.eol())
        last = b"0" + rng.choice(CHUNK_EXTS) + self.eol()
        for _ in range(rng.randrange(0, 3)):
            last += rng.choice(TRAILERS) + self.eol()
        last += self.eol()
        out.append(last)
        return b"".join(out)


def build_request(rng):
    b = Builder(rng)
    parts = [b.request_line()]
    for _ in range(rng.randrange(0, 5)):
        parts.append(b.header_line())
    framing, body_kind, body_len = b.framing_lines()
    for line in framing:
        parts.insert(rng.randrange(1, len(parts) + 1), line)
    parts.append(b.eol())  # the empty line that ends the header section
    if body_kind == "cl":
        parts.append(b.printable(body_len + rng.choice([0, 0, 0, -1, 1])) if rng.random() < 0.9 else b"")
    elif body_kind == "chunked":
        parts.append(b.chunked_body())
    elif rng.random() < 0.3:
        parts.append(b.printable(rng.randrange(0, 6)))
    if rng.random() < 0.15:
        parts.append(b"GET /next HTTP/1.1" + CRLF + CRLF)  # a pipelined second request
    return b"".join(parts)


def mutate(rng, data):
    """Changes a few bytes of an assembled request. Often this breaks a rule."""
    data = bytearray(data)
    for _ in range(rng.randrange(1, 4)):
        op = rng.randrange(4)
        if op == 0 and data:
            data[rng.randrange(len(data))] = rng.choice(INTERESTING)
        elif op == 1:
            data.insert(rng.randrange(len(data) + 1), rng.choice(INTERESTING))
        elif op == 2 and data:
            del data[rng.randrange(len(data))]
        elif op == 3:
            del data[rng.randrange(len(data) + 1):]  # truncate
    return bytes(data)


def generate_case(rng):
    data = build_request(rng)
    if rng.random() < 0.25:
        data = mutate(rng, data)
    return data


def cases(seed, count):
    """Yields (case_id, bytes) for count cases. The same seed gives the same cases."""
    rng = random.Random(seed)
    for i in range(count):
        yield "g%d-%07d" % (seed, i), generate_case(rng)
