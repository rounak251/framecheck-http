"""Seeded grammar-based generator and mutator for harness cases.

The generator builds requests from the grammar in docs/spec-table.md, and most
of them are valid. The mutator then breaks about half of them with small byte
edits, so that the rejection rows are exercised too. Output is JSON lines in
the harness input format (docs/harness-protocol.md).

The same seed and count always produce the same file, byte for byte, so a
differential run can be repeated exactly.

    python3 -I oracle/generate.py --seed 1 --count 2000 --out cases.jsonl
"""

import argparse
import json
import random
import re
import string

CRLF = b"\r\n"
SP = 0x20
METHODS = [b"GET", b"POST", b"PUT", b"HEAD", b"DELETE"]
TARGETS = [b"/", b"/index.html", b"/a/b?c=d", b"*"]
HEADER_NAMES = [b"Host", b"User-Agent", b"Accept", b"X-Test", b"Cookie"]
VALUE_CHARS = (string.ascii_letters + string.digits + " -_./=;,").encode("ascii")

# The framing shapes the grammar can produce. Only the first three are valid
# by construction; the rest are rejection rows on purpose.
FRAMING = ["none", "length", "chunked", "dup_length", "cl_te",
           "te_not_final", "te_gzip_chunked", "te_params"]

# Bytes that often matter to a framing parser. The mutator inserts these.
INTERESTING = [0x00, 0x09, 0x0A, 0x0D, 0x20, 0x22, 0x2C, 0x3A, 0x3B, 0x3D,
               0x5C, 0x7F, 0x80, 0xFF] + [ord(c) for c in "0129aAfFgxX"]

TE_VARIANTS = [b"chunked", b"Chunked", b"chunked, identity", b"gzip",
               b"xchunked", b"chunked;q=1", b"gzip,,chunked", b"chunked, chunked", b""]


def gen_bytes(rng, count):
    return bytes(rng.randrange(256) for _ in range(count))


def gen_value(rng):
    """A header value. Optional leading and trailing OWS is valid and is trimmed."""
    body = bytes(rng.choice(VALUE_CHARS) for _ in range(rng.randrange(0, 13)))
    pad = b" " * rng.randrange(0, 2)
    return pad + body.strip(b" ") + pad


def gen_extension(rng):
    if rng.random() < 0.7:
        return b""
    name = rng.choice([b"ext", b"flag", b"name1"])
    if rng.random() < 0.5:
        return b";" + name
    value = rng.choice([b"token", b'"quoted value"', b'"a\\"b"'])
    return b" ;" + name + b"=" + value


def gen_chunked(rng):
    out = b""
    for _ in range(rng.randrange(0, 4)):
        data = gen_bytes(rng, rng.randrange(1, 20))
        size = b"%x" % len(data)
        if rng.random() < 0.3:
            size = size.upper()
        if rng.random() < 0.2:
            size = b"0" + size
        out += size + gen_extension(rng) + CRLF + data + CRLF
    out += b"0" + gen_extension(rng) + CRLF
    for _ in range(rng.randrange(0, 2)):
        out += rng.choice(HEADER_NAMES) + b":" + gen_value(rng) + CRLF
    return out + CRLF


def gen_request(rng, pipelined=True):
    """One request from the grammar. With some probability a second one follows it."""
    version = rng.choice([b"HTTP/1.1", b"HTTP/1.1", b"HTTP/1.1", b"HTTP/1.0"])
    request_line = b" ".join([rng.choice(METHODS), rng.choice(TARGETS), version])
    headers = []
    for _ in range(rng.randrange(0, 4)):
        headers.append(rng.choice(HEADER_NAMES) + b":" + gen_value(rng))

    framing = rng.choice(FRAMING)
    body = b""
    if framing == "length":
        body = gen_bytes(rng, rng.randrange(0, 30))
        headers.append(b"Content-Length: %d" % len(body))
    elif framing == "dup_length":
        count = rng.randrange(0, 20)
        other = count if rng.random() < 0.5 else count + 1
        headers.append(b"Content-Length: %d" % count)
        headers.append(b"Content-Length: %d" % other)
        body = gen_bytes(rng, count)
    elif framing == "chunked":
        body = gen_chunked(rng)
        headers.append(b"Transfer-Encoding: chunked")
    elif framing == "te_gzip_chunked":
        body = gen_chunked(rng)
        headers.append(b"Transfer-Encoding: gzip, chunked")
    elif framing == "te_not_final":
        body = gen_chunked(rng)
        headers.append(b"Transfer-Encoding: chunked, identity")
    elif framing == "te_params":
        body = gen_chunked(rng)
        headers.append(b"Transfer-Encoding: chunked;q=1")
    elif framing == "cl_te":
        body = gen_chunked(rng)
        headers.append(b"Content-Length: 5")
        headers.append(b"Transfer-Encoding: chunked")

    rng.shuffle(headers)
    message = CRLF.join([request_line] + headers) + CRLF + CRLF + body
    if pipelined and rng.random() < 0.2:
        message += gen_request(rng, pipelined=False)
    return message


def _lines(data):
    return bytes(data).split(CRLF)


def op_set_byte(rng, data):
    if data:
        data[rng.randrange(len(data))] = rng.choice(INTERESTING)
    return data


def op_insert_byte(rng, data):
    data.insert(rng.randrange(len(data) + 1), rng.choice(INTERESTING))
    return data


def op_delete_byte(rng, data):
    if data:
        del data[rng.randrange(len(data))]
    return data


def op_truncate(rng, data):
    # Cutting the input short exercises the incomplete rows.
    return bytearray(data[:rng.randrange(len(data) + 1)])


def op_bare_lf(rng, data):
    spots = [m.start() for m in re.finditer(CRLF, bytes(data))]
    if spots:
        at = rng.choice(spots)
        del data[at]
    return data


def op_space_before_colon(rng, data):
    spots = [m.start() for m in re.finditer(b":", bytes(data))]
    if spots:
        data.insert(rng.choice(spots), SP)
    return data



def op_fold_line(rng, data):
    lines = _lines(data)
    if len(lines) > 2:
        i = rng.randrange(1, len(lines) - 1)
        lines[i] = b" " + lines[i]
    return bytearray(CRLF.join(lines))


def op_duplicate_line(rng, data):
    lines = _lines(data)
    i = rng.randrange(len(lines))
    lines.insert(i, lines[i])
    return bytearray(CRLF.join(lines))


def op_content_length(rng, data):
    value = rng.choice([b"0", b"1", b"5", b"-1", b"9223372036854775808", b"5, 5"])
    return bytearray(re.sub(rb"(?i)content-length:[^\r\n]*",
                            b"Content-Length: " + value, bytes(data), count=1))


def op_transfer_encoding(rng, data):
    value = rng.choice(TE_VARIANTS)
    return bytearray(re.sub(rb"(?i)transfer-encoding:[^\r\n]*",
                            b"Transfer-Encoding: " + value, bytes(data), count=1))


OPERATIONS = [op_set_byte, op_insert_byte, op_delete_byte, op_truncate,
              op_bare_lf, op_space_before_colon, op_fold_line,
              op_duplicate_line, op_content_length, op_transfer_encoding]


def mutate(rng, data):
    """Apply one to three random edits. Each edit is a single small change."""
    out = bytearray(data)
    for _ in range(rng.randrange(1, 4)):
        out = rng.choice(OPERATIONS)(rng, out)
    return bytes(out)


def make_cases(seed, count):
    """Yield (case_id, bytes) pairs. The id says whether a case was mutated."""
    rng = random.Random(seed)
    for n in range(count):
        data = gen_request(rng)
        kind = "g"
        if rng.random() < 0.5:
            data = mutate(rng, data)
            kind = "m"
        yield "%s%d-%07d" % (kind, seed, n), data


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    written = 0
    with open(args.out, "w", encoding="utf-8") as handle:
        for case_id, data in make_cases(args.seed, args.count):
            handle.write(json.dumps({"id": case_id, "hex": data.hex()}) + "\n")
            written += 1
    print("wrote %d cases to %s" % (written, args.out))


if __name__ == "__main__":
    main()
