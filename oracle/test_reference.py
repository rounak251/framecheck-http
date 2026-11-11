"""Unit tests for oracle/reference.py. One group per row of docs/spec-table.md.

Run from the project directory:

    python3 -I -m unittest discover -s oracle -p 'test_*.py' -v
"""

import os
import subprocess
import sys
import unittest

import reference

HERE = os.path.dirname(os.path.abspath(__file__))
REQUEST_LINE = b"GET / HTTP/1.1\r\n"          # 16 bytes; used for offsets below
CONTENT_LENGTH_5 = b"Content-Length: 5"   # 17 bytes, plus CRLF = 19 on the wire


def request(headers=(), body=b"", line=REQUEST_LINE):
    """A request with the given header lines, a blank line and a body."""
    out = line
    for header in headers:
        out += header + b"\r\n"
    return out + b"\r\n" + body


def chunks(*parts, last=b"0\r\n\r\n"):
    """A chunked body with one chunk per part, then the last chunk."""
    out = b""
    for part in parts:
        out += b"%x\r\n%s\r\n" % (len(part), part)
    return out + last


def error(data):
    result = reference.parse(data)
    assert result.status == "error", result
    return result.error_class, result.error_offset


def complete(data):
    result = reference.parse(data)
    assert result.status == "complete", result
    return result.body_length, result.consumed


class GeneralRules(unittest.TestCase):
    def test_G01_bare_lf_in_request_line(self):
        self.assertEqual(error(b"GET / HTTP/1.1\n\r\n"), ("bare-lf", 14))

    def test_G01_bare_lf_in_header(self):
        data = b"GET / HTTP/1.1\r\nHost: x\n\r\n"
        self.assertEqual(error(data), ("bare-lf", 23))

    def test_G02_bare_cr_followed_by_text(self):
        self.assertEqual(error(b"GET / HTTP/1.1\rX"), ("bare-cr", 14))

    def test_G03_eager_error_before_input_ends(self):
        # The NUL is the first byte that cannot be in a target. The input ends
        # later, but the answer is already an error.
        self.assertEqual(error(b"GET /\x00"), ("bad-request-line", 5))

    def test_G03_eager_field_name_error_before_colon_arrives(self):
        self.assertEqual(error(REQUEST_LINE + b"Bad Name"), ("ws-before-colon", 19))

    def test_G03_incomplete_when_nothing_is_wrong(self):
        self.assertEqual(reference.parse(REQUEST_LINE + b"Host: a").status, "incomplete")

    def test_G04_leading_crlf_rejected(self):
        self.assertEqual(error(b"\r\n" + request()), ("bad-request-line", 0))

    def test_G05_pipelined_second_request_is_not_read(self):
        first = request(body=b"")
        data = first + b"GET /2 HTTP/1.1\r\n\r\n"
        self.assertEqual(complete(data), (0, len(first)))


class RequestLineRules(unittest.TestCase):
    def test_RL01_double_space_rejected(self):
        self.assertEqual(error(b"GET  / HTTP/1.1\r\n\r\n"), ("bad-request-line", 4))

    def test_RL01_missing_space_before_version(self):
        self.assertEqual(error(b"GET /HTTP/1.1\r\n\r\n")[0], "bad-request-line")

    def test_RL02_non_token_method_rejected(self):
        self.assertEqual(error(b"G@T / HTTP/1.1\r\n\r\n"), ("bad-request-line", 1))

    def test_RL02_lower_case_method_is_a_valid_token(self):
        self.assertEqual(complete(b"get / HTTP/1.1\r\n\r\n"), (0, 18))

    def test_RL03_control_byte_in_target(self):
        self.assertEqual(error(b"GET /\x7f HTTP/1.1\r\n\r\n"), ("bad-request-line", 5))

    def test_RL03_obs_text_in_target(self):
        self.assertEqual(error(b"GET /\x80 HTTP/1.1\r\n\r\n"), ("bad-request-line", 5))

    def test_RL04_http_1_0_accepted(self):
        self.assertEqual(complete(b"GET / HTTP/1.0\r\n\r\n"), (0, 18))

    def test_RL04_http_1_2_rejected(self):
        self.assertEqual(error(b"GET / HTTP/1.2\r\n\r\n"), ("bad-request-line", 13))

    def test_RL04_http_2_0_rejected(self):
        self.assertEqual(error(b"GET / HTTP/2.0\r\n\r\n")[0], "bad-request-line")

    def test_RL04_lower_case_version_rejected(self):
        self.assertEqual(error(b"GET / http/1.1\r\n\r\n"), ("bad-request-line", 6))


class FieldLineRules(unittest.TestCase):
    def test_HF01_space_before_colon(self):
        self.assertEqual(error(request([b"Host : x"])), ("ws-before-colon", 20))

    def test_HF01_tab_before_colon(self):
        self.assertEqual(error(request([b"Host\t: x"]))[0], "ws-before-colon")

    def test_HF02_empty_name(self):
        self.assertEqual(error(request([b": x"])), ("bad-field-name", 16))

    def test_HF02_non_token_byte_in_name(self):
        self.assertEqual(error(request([b"Ho@st: x"])), ("bad-field-name", 18))

    def test_HF02_line_without_colon(self):
        self.assertEqual(error(REQUEST_LINE + b"Host\r\n\r\n"), ("bad-field-name", 20))

    def test_HF03_folded_continuation_line(self):
        data = request([b"X-A: 1", b" folded"])
        self.assertEqual(error(data), ("obs-fold", 24))

    def test_HF03_tab_led_continuation_line(self):
        self.assertEqual(error(request([b"X-A: 1", b"\tfolded"]))[0], "obs-fold")

    def test_HF03_whitespace_line_right_after_request_line(self):
        self.assertEqual(error(REQUEST_LINE + b" X: 1\r\n\r\n"), ("obs-fold", 16))

    def test_HF04_control_byte_in_value(self):
        self.assertEqual(error(request([b"X-A: a\x01b"])), ("bad-field-value", 22))

    def test_HF04_del_in_value(self):
        self.assertEqual(error(request([b"X-A: \x7f"]))[0], "bad-field-value")

    def test_HF04_obs_text_in_value_rejected(self):
        self.assertEqual(error(request([b"X-A: \xe9"]))[0], "bad-field-value")

    def test_HF04_trailing_spaces_are_trimmed_before_length_parse(self):
        self.assertEqual(complete(request([b"Content-Length: 5   "], body=b"hello")), (5, 16 + 22 + 2 + 5))

    def test_HF04_tab_after_colon_is_trimmed(self):
        self.assertEqual(complete(request([b"Content-Length:\t5"], body=b"hello"))[0], 5)

    def test_HF05_empty_value_accepted(self):
        self.assertEqual(complete(request([b"X-Empty:"])), (0, 16 + 10 + 2))

    def test_HF06_empty_line_ends_header_section(self):
        self.assertEqual(complete(request([b"Host: x"])), (0, 16 + 9 + 2))


class FramingRules(unittest.TestCase):
    def test_FR01_content_length_and_transfer_encoding(self):
        data = request([CONTENT_LENGTH_5, b"Transfer-Encoding: chunked"])
        self.assertEqual(error(data), ("cl-te-conflict", 16 + 19))

    def test_FR01_transfer_encoding_before_content_length(self):
        data = request([b"Transfer-Encoding: chunked", CONTENT_LENGTH_5])
        self.assertEqual(error(data), ("cl-te-conflict", 16 + 28))

    def test_FR02_duplicate_content_length_differing(self):
        data = request([CONTENT_LENGTH_5, b"Content-Length: 6"])
        self.assertEqual(error(data), ("duplicate-content-length", 16 + 19))

    def test_FR02_duplicate_content_length_identical_accepted(self):
        data = request([CONTENT_LENGTH_5, CONTENT_LENGTH_5], body=b"hello")
        self.assertEqual(complete(data)[0], 5)

    def test_FR02_duplicates_equal_as_decimal_numbers(self):
        data = request([b"Content-Length: 05", CONTENT_LENGTH_5], body=b"hello")
        self.assertEqual(complete(data)[0], 5)

    def test_FR03_comma_list_rejected_even_when_identical(self):
        self.assertEqual(error(request([b"Content-Length: 5, 5"])), ("bad-content-length", 16))

    def test_FR03_plus_sign_rejected(self):
        self.assertEqual(error(request([b"Content-Length: +5"]))[0], "bad-content-length")

    def test_FR03_empty_value_rejected(self):
        self.assertEqual(error(request([b"Content-Length:"]))[0], "bad-content-length")

    def test_FR03_space_inside_value_rejected(self):
        self.assertEqual(error(request([b"Content-Length: 5 5"]))[0], "bad-content-length")

    def test_FR03_largest_allowed_value_is_incomplete_without_body(self):
        data = request([b"Content-Length: 9223372036854775807"])
        self.assertEqual(reference.parse(data).status, "incomplete")

    def test_FR03_value_over_cap_rejected(self):
        data = request([b"Content-Length: 9223372036854775808"])
        self.assertEqual(error(data)[0], "bad-content-length")

    def test_FR03_zero_length_body(self):
        self.assertEqual(complete(request([b"Content-Length: 0"])), (0, 16 + 19 + 2))

    def test_FR04_two_transfer_encoding_lines(self):
        data = request([b"Transfer-Encoding: gzip", b"Transfer-Encoding: chunked"])
        self.assertEqual(error(data), ("bad-transfer-encoding", 16 + 25))

    def test_FR05_parameter_on_chunked_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: chunked;q=1"]))[0],
                         "bad-transfer-encoding")

    def test_FR05_empty_list_element_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: gzip,,chunked"]))[0],
                         "bad-transfer-encoding")

    def test_FR05_trailing_comma_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: chunked,"]))[0],
                         "bad-transfer-encoding")

    def test_FR05_owss_around_commas_accepted(self):
        data = request([b"Transfer-Encoding: gzip , chunked"], body=chunks(b"ab"))
        self.assertEqual(complete(data)[0], 2)

    def test_FR06_final_coding_gzip_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: gzip"]))[0], "bad-transfer-encoding")

    def test_FR06_chunked_then_identity_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: chunked, identity"])),
                         ("bad-transfer-encoding", 16))

    def test_FR06_xchunked_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: xchunked"]))[0],
                         "bad-transfer-encoding")

    def test_FR06_chunked_is_case_insensitive(self):
        data = request([b"Transfer-Encoding: CHUNKED"], body=chunks(b"ab"))
        self.assertEqual(complete(data)[0], 2)

    def test_FR07_chunked_twice_rejected(self):
        self.assertEqual(error(request([b"Transfer-Encoding: chunked, chunked"]))[0],
                         "bad-transfer-encoding")

    def test_FR08_non_final_gzip_is_framed_as_chunked(self):
        data = request([b"Transfer-Encoding: gzip, chunked"], body=chunks(b"\x1f\x8b"))
        self.assertEqual(complete(data)[0], 2)

    def test_FR09_http_1_0_with_transfer_encoding_rejected(self):
        data = request([b"Transfer-Encoding: chunked"], line=b"GET / HTTP/1.0\r\n")
        self.assertEqual(error(data), ("bad-transfer-encoding", 16))

    def test_FR10_no_framing_means_no_body(self):
        self.assertEqual(complete(request([b"Host: x"], body=b"ignored")), (0, 16 + 9 + 2))

    def test_FR11_content_length_body_complete(self):
        self.assertEqual(complete(request([CONTENT_LENGTH_5], body=b"hello")),
                         (5, 16 + 19 + 2 + 5))

    def test_FR11_content_length_body_short_is_incomplete(self):
        self.assertEqual(reference.parse(request([CONTENT_LENGTH_5], body=b"hel")).status,
                         "incomplete")

    def test_FR11_bytes_after_body_are_the_next_message(self):
        data = request([CONTENT_LENGTH_5], body=b"helloGET / HTTP/1.1\r\n\r\n")
        self.assertEqual(complete(data), (5, 16 + 19 + 2 + 5))


class ChunkedRules(unittest.TestCase):
    TE = [b"Transfer-Encoding: chunked"]
    HEADER_END = 16 + 28 + 2  # request line, TE header line, blank line

    def test_CH01_upper_and_lower_hex_and_leading_zeros(self):
        body = b"000A\r\n0123456789\r\n0\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 10)
        body = b"a\r\n0123456789\r\n0\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 10)

    def test_CH01_chunk_size_with_no_digits(self):
        self.assertEqual(error(request(self.TE, body=b"\r\n")),
                         ("bad-chunk-size", self.HEADER_END))

    def test_CH02_value_over_cap_overflows_at_the_digit_that_exceeds(self):
        # 2^63 needs 16 hex digits; the 16th digit is the one that exceeds the cap.
        body = b"8000000000000000\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("chunk-size-overflow", self.HEADER_END + 15))

    def test_CH02_largest_allowed_size_is_incomplete_without_data(self):
        body = b"7FFFFFFFFFFFFFFF\r\n"
        self.assertEqual(reference.parse(request(self.TE, body=body)).status, "incomplete")

    def test_CH03_unknown_extension_is_ignored(self):
        body = b"5;unknown=1\r\nhello\r\n0\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 5)

    def test_CH03_extension_without_value_and_with_bws(self):
        body = b"5 ; flag\r\nhello\r\n0\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 5)

    def test_CH03_quoted_string_value(self):
        body = b'5;q="a b"\r\nhello\r\n0\r\n\r\n'
        self.assertEqual(complete(request(self.TE, body=body))[0], 5)

    def test_CH03_quoted_pair_inside_quoted_string(self):
        body = b'5;q="a\\"b"\r\nhello\r\n0\r\n\r\n'
        self.assertEqual(complete(request(self.TE, body=body))[0], 5)

    def test_CH03_space_after_size_without_semicolon_rejected(self):
        self.assertEqual(error(request(self.TE, body=b"5 \r\n"))[0], "bad-chunk-size")

    def test_CH03_semicolon_with_no_name_rejected(self):
        self.assertEqual(error(request(self.TE, body=b"5;\r\n"))[0], "bad-chunk-ext")

    def test_CH03_obs_text_in_quoted_string_rejected(self):
        self.assertEqual(error(request(self.TE, body=b'5;q="\x80"\r\n'))[0], "bad-chunk-ext")

    def test_CH03_unterminated_quoted_string_rejected(self):
        self.assertEqual(error(request(self.TE, body=b'5;q="ab\r\n'))[0], "bad-chunk-ext")

    def test_CH04_bare_lf_after_chunk_size(self):
        self.assertEqual(error(request(self.TE, body=b"5\nhello"))[0], "bare-lf")

    def test_CH05_chunk_data_may_contain_crlf(self):
        body = b"6\r\nab\r\ncd\r\n0\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 6)

    def test_CH05_missing_crlf_after_data(self):
        body = b"5\r\nhelloXX"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-chunk-data-terminator", self.HEADER_END + 3 + 5))

    def test_CH06_zero_with_zeros_and_extension(self):
        self.assertEqual(complete(request(self.TE, body=b"00\r\n\r\n"))[0], 0)
        self.assertEqual(complete(request(self.TE, body=b"0;ext=1\r\n\r\n"))[0], 0)

    def test_CH07_trailer_field_accepted(self):
        body = b"0\r\nX-Trailer: v\r\n\r\n"
        self.assertEqual(complete(request(self.TE, body=body))[0], 0)

    def test_CH07_trailer_with_whitespace_before_colon(self):
        body = b"0\r\nX Trailer: v\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body))[0], "bad-trailer")

    def test_CH07_trailer_obs_fold(self):
        body = b"0\r\nX-T: v\r\n folded\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body))[0], "bad-trailer")

    def test_CH07_trailer_bad_value(self):
        body = b"0\r\nX-T: a\x7fb\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body))[0], "bad-trailer")

    def test_CH08_trailer_content_length_rejected(self):
        body = b"0\r\nContent-Length: 5\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-trailer", self.HEADER_END + 3))

    def test_CH08_trailer_transfer_encoding_rejected_in_any_case(self):
        body = b"0\r\ntransfer-encoding: chunked\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body))[0], "bad-trailer")

    def test_CH09_body_length_is_sum_and_consumed_is_exact(self):
        body = b"3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n"
        data = request(self.TE, body=body + b"NEXT")
        self.assertEqual(complete(data), (5, self.HEADER_END + len(body)))


class ReviewRound2Rules(unittest.TestCase):
    """Cases fixed in review round 2 (docs/disagreements.md, groups A to F)."""
    TE = [b"Transfer-Encoding: chunked"]
    HEADER_END = 16 + 28 + 2

    def test_CH03_space_before_crlf_after_extension_is_bad_chunk_ext(self):
        # Group A and B: after a ';' the error is bad-chunk-ext, at the byte after the space.
        body = b"5;a \r\nhello\r\n0\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-chunk-ext", self.HEADER_END + 4))

    def test_CH03_space_between_extension_value_and_next_byte_is_bad_chunk_ext(self):
        # Group A: "c" is the first byte that cannot continue the extension.
        body = b"0;a=b c\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-chunk-ext", self.HEADER_END + 6))

    def test_CH03_space_after_size_then_crlf_reports_the_crlf(self):
        # Group C: no extension, so the class is bad-chunk-size, at the CR (not the space).
        body = b"0 \r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-chunk-size", self.HEADER_END + 2))

    def test_CH03_space_after_size_then_cr_without_lf_is_bare_cr(self):
        # Group B: a bare CR wins over the size class (G-02).
        body = b"3 \r\rabc"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bare-cr", self.HEADER_END + 2))

    def test_CH08_framing_name_with_space_before_colon_is_bad_trailer_at_line_start(self):
        # Group D: CH-08 is checked before HF-01, so the line start is reported.
        body = b"0\r\nContent-Length : 0\r\n\r\n"
        self.assertEqual(error(request(self.TE, body=body)),
                         ("bad-trailer", self.HEADER_END + 3))

    def test_FR02_before_FR03_different_invalid_values_are_duplicates(self):
        # Group E: the second line differs from the first, so FR-02 fires first.
        data = request([b"Content-Length: ;1", b"Content-Length: 5"])
        self.assertEqual(error(data), ("duplicate-content-length", 16 + 20))

    def test_FR02_equal_invalid_values_are_bad_content_length(self):
        data = request([b"Content-Length: x", b"Content-Length: x"])
        self.assertEqual(error(data), ("bad-content-length", 16))


class ConnectionRules(unittest.TestCase):
    def test_CN01_error_sets_close(self):
        result = reference.respond(
            '{"id": "x", "hex": "%s"}' % (b"GET / HTTP/1.1\n".hex()))
        self.assertEqual(result["status"], "error")
        self.assertTrue(result["close"])

    def test_CN01_complete_does_not_set_close(self):
        result = reference.respond('{"id": "x", "hex": "%s"}' % (request().hex()))
        self.assertEqual(result["status"], "complete")
        self.assertFalse(result["close"])


class HarnessProtocol(unittest.TestCase):
    def test_docs_examples_match_the_oracle(self):
        ex1 = ('{"id":"ex-1","hex":"504f5354202f20485454502f312e310d0a436f6e74656e742d'
               '4c656e6774683a20350d0a0d0a68656c6c6f474554202f"}')
        ex2 = ('{"id":"ex-2","hex":"504f5354202f20485454502f312e310d0a436f6e74656e742d'
               '4c656e6774683a20350d0a5472616e736665722d456e636f64696e673a206368756e6b6564'
               '0d0a0d0a"}')
        ex3 = '{"id":"ex-3","hex":"474554202f20485454502f312e310a"}'
        self.assertEqual(reference.respond(ex1)["consumed"], 43)
        self.assertEqual(reference.respond(ex2)["error_offset"], 36)
        self.assertEqual(reference.respond(ex3)["error_offset"], 14)

    def test_bad_hex_is_bad_input(self):
        self.assertEqual(reference.respond('{"id": "h", "hex": "abc"}')["status"], "bad-input")
        self.assertEqual(reference.respond('{"id": "h", "hex": "zz"}')["status"], "bad-input")

    def test_hex_with_spaces_is_bad_input(self):
        self.assertEqual(reference.respond('{"id": "h", "hex": "47 45"}')["status"], "bad-input")

    def test_not_json_is_bad_input_with_null_id(self):
        result = reference.respond("not json")
        self.assertEqual(result["status"], "bad-input")
        self.assertIsNone(result["id"])

    def test_non_string_id_is_echoed_as_null(self):
        result = reference.respond('{"id": 7, "hex": ""}')
        self.assertEqual(result["status"], "bad-input")
        self.assertIsNone(result["id"])

    def test_empty_hex_is_incomplete(self):
        self.assertEqual(reference.respond('{"id": "e", "hex": ""}')["status"], "incomplete")

    def test_main_skips_blank_lines_and_keeps_order(self):
        lines = ('\n{"id":"a","hex":""}\n   \n{"id":"b","hex":"%s"}\n'
                 % request().hex())
        proc = subprocess.run([sys.executable, "-I", os.path.join(HERE, "reference.py")],
                              input=lines.encode("utf-8"), capture_output=True, check=True)
        out = proc.stdout.decode("utf-8").splitlines()
        self.assertEqual(len(out), 2)
        self.assertIn('"id": "a"', out[0])
        self.assertIn('"status": "complete"', out[1])


if __name__ == "__main__":
    unittest.main()
