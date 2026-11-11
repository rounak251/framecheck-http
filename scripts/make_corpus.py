#!/usr/bin/env python3
"""Writes the smuggling corpus and the valid seed corpus as raw .http files.

Each smuggling case is a request that docs/spec-table.md says must be rejected.
Each valid seed is a request that the spec table says must be accepted. The
fuzzer mutates both sets. Run `python3 scripts/make_corpus.py` from the project
root to regenerate them.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

POST = b"POST / HTTP/1.1\r\nHost: a.example\r\n"
GET = b"GET / HTTP/1.1\r\nHost: a.example\r\n"
CHUNKED = b"Transfer-Encoding: chunked\r\n"

# name -> bytes. Each one must come out as an error in the parser.
SMUGGLING = {
    # CL together with TE (FR-01)
    "cl-te-basic": POST + b"Content-Length: 5\r\n" + CHUNKED + b"\r\n0\r\n\r\n",
    "cl-te-reversed": POST + CHUNKED + b"Content-Length: 5\r\n\r\n0\r\n\r\n",
    "cl-zero-te": POST + b"Content-Length: 0\r\n" + CHUNKED + b"\r\n0\r\n\r\n",
    "cl-te-identical-cl": POST + b"Content-Length: 4\r\nContent-Length: 4\r\n" + CHUNKED + b"\r\n",
    "cl-te-te-gzip": POST + b"Content-Length: 4\r\nTransfer-Encoding: gzip\r\n\r\n",
    # Duplicate Content-Length (FR-02)
    "dup-cl-different": POST + b"Content-Length: 5\r\nContent-Length: 6\r\n\r\nhello",
    "dup-cl-three-values": POST + b"Content-Length: 5\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
    "dup-cl-second-smaller": POST + b"Content-Length: 12\r\nContent-Length: 0\r\n\r\n",
    # Content-Length value that is not one decimal (FR-03)
    "cl-comma-different": POST + b"Content-Length: 5, 6\r\n\r\n",
    "cl-comma-identical": POST + b"Content-Length: 5, 5\r\n\r\n",
    "cl-plus-sign": POST + b"Content-Length: +5\r\n\r\n",
    "cl-minus-sign": POST + b"Content-Length: -5\r\n\r\n",
    "cl-hex-prefix": POST + b"Content-Length: 0x5\r\n\r\n",
    "cl-overflow-2pow64": POST + b"Content-Length: 18446744073709551616\r\n\r\n",
    "cl-empty": POST + b"Content-Length:\r\n\r\n",
    "cl-trailing-letters": POST + b"Content-Length: 5abc\r\n\r\nhello",
    "cl-internal-space": POST + b"Content-Length: 1 2\r\n\r\n",
    # Transfer-Encoding whose final coding is not chunked (FR-06, FR-07)
    "te-xchunked": POST + b"Transfer-Encoding: xchunked\r\n\r\n",
    "te-chunked-identity": POST + b"Transfer-Encoding: chunked, identity\r\n\r\n",
    "te-gzip-only": POST + b"Transfer-Encoding: gzip\r\n\r\n",
    "te-chunked-twice": POST + b"Transfer-Encoding: chunked, chunked\r\n\r\n",
    "te-chunked-prefix-suffix": POST + b"Transfer-Encoding: chunkedx\r\n\r\n",
    # Duplicate Transfer-Encoding lines (FR-04)
    "te-duplicate-lines": POST + CHUNKED + CHUNKED + b"\r\n0\r\n\r\n",
    "te-second-line-identity": POST + CHUNKED + b"Transfer-Encoding: identity\r\n\r\n",
    # Transfer-Encoding list syntax (FR-05)
    "te-parameter": POST + b"Transfer-Encoding: chunked;q=1\r\n\r\n",
    "te-empty-element": POST + b"Transfer-Encoding: gzip,,chunked\r\n\r\n",
    "te-trailing-comma": POST + b"Transfer-Encoding: chunked,\r\n\r\n",
    "te-leading-comma": POST + b"Transfer-Encoding: ,chunked\r\n\r\n",
    # Transfer-Encoding on HTTP/1.0 (FR-09)
    "te-http10": b"POST / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
    # Whitespace before the colon and a leading space (HF-01, HF-03)
    "te-ws-before-colon": POST + b"Transfer-Encoding : chunked\r\n\r\n",
    "cl-ws-before-colon": POST + b"Content-Length : 5\r\n\r\nhello",
    "te-tab-before-colon": POST + b"Transfer-Encoding\t: chunked\r\n\r\n",
    "leading-space-te": POST + b" Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
    "leading-space-first-header": b"GET / HTTP/1.1\r\n Host: a\r\n\r\n",
    "leading-tab-cl": POST + b"\tContent-Length: 5\r\n\r\nhello",
    # Obsolete line folding (HF-03)
    "obs-fold-cl": POST + b"Content-Length:\r\n 5\r\n\r\nhello",
    "obs-fold-te": POST + b"Transfer-Encoding:\r\n chunked\r\n\r\n",
    "obs-fold-header-continuation": POST + b"X-A: 1\r\n\tTransfer-Encoding: chunked\r\n\r\n",
    # Bare LF and bare CR (G-01, G-02)
    "bare-lf-te-line": POST + b"Transfer-Encoding: chunked\n\r\n0\r\n\r\n",
    "bare-lf-request-line": b"POST / HTTP/1.1\nContent-Length: 5\r\n\r\nhello",
    "bare-cr-in-cl-value": POST + b"Content-Length: 5\r\rX\r\n\r\n",
    "bare-lf-chunk-size": POST + CHUNKED + b"\r\n3\nabc\r\n0\r\n\r\n",
    "bare-lf-trailer": POST + CHUNKED + b"\r\n0\r\nX-A: 1\n\r\n",
    # Request line problems (G-04, RL-01, RL-04)
    "leading-space-request-line": b" POST / HTTP/1.1\r\nHost: a\r\n\r\n",
    "empty-line-before-request": b"\r\nPOST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello",
    "http2-version-string": b"POST / HTTP/2.0\r\nContent-Length: 5\r\n\r\nhello",
    # Chunk framing (CH-02, CH-03, CH-05)
    "chunk-size-overflow": POST + CHUNKED + b"\r\n8000000000000000\r\n",
    "chunk-size-space-before-crlf": POST + CHUNKED + b"\r\n3 \r\nabc\r\n0\r\n\r\n",
    "chunk-ext-bad-quote": POST + CHUNKED + b"\r\n3;a=\"\xe9\"\r\nabc\r\n0\r\n\r\n",
    "chunk-data-no-crlf": POST + CHUNKED + b"\r\n3\r\nabcXY0\r\n\r\n",
    # Trailer fields that carry framing (CH-07, CH-08)
    "trailer-content-length": POST + CHUNKED + b"\r\n0\r\nContent-Length: 0\r\n\r\n",
    "trailer-transfer-encoding": POST + CHUNKED + b"\r\n0\r\nTransfer-Encoding: chunked\r\n\r\n",
    "trailer-obs-fold": POST + CHUNKED + b"\r\n0\r\nX-A: 1\r\n 2\r\n\r\n",
    # Bytes that are not allowed in a field (HF-02, HF-04)
    "nul-in-header-name": POST + b"Trans\x00fer-Encoding: chunked\r\n\r\n",
    "obs-text-in-te-value": POST + b"Transfer-Encoding: chunked\xe9\r\n\r\n",
    "control-byte-in-cl-value": POST + b"Content-Length: 5\x01\r\n\r\nhello",
    "slash-in-header-name": POST + b"Transfer/Encoding: chunked\r\n\r\n",
}

# Each one must come out as complete. The fuzzer starts from these.
VALID = {
    "get-minimal": GET + b"\r\n",
    "post-content-length": POST + b"Content-Length: 11\r\n\r\nhello world",
    "post-chunked-ext-trailer": POST + CHUNKED + b"\r\n4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\nX-T: v\r\n\r\n",
    "post-gzip-chunked": POST + b"Transfer-Encoding: gzip, chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n",
    "post-dup-cl-same": POST + b"Content-Length: 3\r\nContent-Length: 3\r\n\r\nabc",
    "chunk-quoted-ext": POST + CHUNKED + b"\r\n3;a=\"x\\\"y\"\r\nabc\r\n0\r\n\r\n",
    "http10-get": b"GET /old HTTP/1.0\r\n\r\n",
    "pipelined-pair": GET + b"Content-Length: 2\r\n\r\nok" + GET + b"\r\n",
    "many-headers": GET + b"Accept: */*\r\nUser-Agent: seed\r\nX-Empty:\r\nCookie: a=b; c=d\r\n\r\n",
    "mixed-case-te": POST + b"transfer-encoding: CHUNKED\r\n\r\n0\r\n\r\n",
}


def write_set(directory, cases):
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob("*.http"):
        old.unlink()
    for name, data in sorted(cases.items()):
        (directory / (name + ".http")).write_bytes(data)
    return len(cases)


def main():
    smuggling = write_set(ROOT / "corpus" / "smuggling", SMUGGLING)
    valid = write_set(ROOT / "corpus" / "valid", VALID)
    print(f"smuggling cases: {smuggling}, valid seeds: {valid}")


if __name__ == "__main__":
    main()
