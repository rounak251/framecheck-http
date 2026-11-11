"""Tests for oracle/generate.py: determinism, valid JSON lines, and outcome mix."""

import collections
import json
import random
import unittest

import generate
import reference

KNOWN_CLASSES = {
    "bare-lf", "bare-cr", "bad-request-line", "ws-before-colon", "bad-field-name",
    "obs-fold", "bad-field-value", "cl-te-conflict", "duplicate-content-length",
    "bad-content-length", "bad-transfer-encoding", "bad-chunk-size",
    "chunk-size-overflow", "bad-chunk-ext", "bad-chunk-data-terminator", "bad-trailer",
}


class Determinism(unittest.TestCase):
    def test_same_seed_gives_same_cases(self):
        first = list(generate.make_cases(42, 300))
        second = list(generate.make_cases(42, 300))
        self.assertEqual(first, second)

    def test_different_seed_gives_different_cases(self):
        first = list(generate.make_cases(1, 50))
        second = list(generate.make_cases(2, 50))
        self.assertNotEqual(first, second)

    def test_ids_are_unique_within_a_run(self):
        ids = [case_id for case_id, _ in generate.make_cases(9, 500)]
        self.assertEqual(len(ids), len(set(ids)))


class OutputFormat(unittest.TestCase):
    def test_every_line_is_a_valid_harness_case(self):
        for case_id, data in generate.make_cases(3, 200):
            line = json.dumps({"id": case_id, "hex": data.hex()})
            response = reference.respond(line)
            self.assertNotEqual(response["status"], "bad-input", line)

    def test_empty_input_is_allowed(self):
        self.assertEqual(reference.respond(json.dumps({"id": "e", "hex": ""}))["status"],
                         "incomplete")


class Outcomes(unittest.TestCase):
    def test_outcome_mix_covers_all_three_statuses(self):
        counts = collections.Counter()
        for _, data in generate.make_cases(7, 600):
            counts[reference.parse(data).status] += 1
        for status in ("complete", "error", "incomplete"):
            self.assertGreater(counts[status], 0, counts)

    def test_error_classes_are_all_from_the_spec(self):
        for _, data in generate.make_cases(11, 600):
            result = reference.parse(data)
            if result.status == "error":
                self.assertIn(result.error_class, KNOWN_CLASSES)

    def test_unmutated_grammar_output_is_mostly_valid(self):
        # Two framing shapes are rejections on purpose, so the valid share is
        # well below 100%. It should still be a large share of the sample.
        rng = random.Random(5)
        complete = sum(reference.parse(generate.gen_request(rng)).status == "complete"
                       for _ in range(400))
        self.assertGreater(complete, 100, complete)


class Mutator(unittest.TestCase):
    def test_mutation_is_deterministic_for_a_seed(self):
        base = b"GET / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello"
        first = generate.mutate(random.Random(3), base)
        second = generate.mutate(random.Random(3), base)
        self.assertEqual(first, second)

    def test_mutator_can_introduce_a_bare_lf(self):
        base = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"

        def has_bare_lf(data):
            return any(data[i] == 0x0A and (i == 0 or data[i - 1] != 0x0D)
                       for i in range(len(data)))

        seen = any(has_bare_lf(generate.op_bare_lf(random.Random(seed), bytearray(base)))
                   for seed in range(200))
        self.assertTrue(seen)

    def test_truncate_makes_a_prefix(self):
        base = b"GET / HTTP/1.1\r\n\r\n"
        for seed in range(50):
            out = bytes(generate.op_truncate(random.Random(seed), bytearray(base)))
            self.assertEqual(base[:len(out)], out)


if __name__ == "__main__":
    unittest.main()
