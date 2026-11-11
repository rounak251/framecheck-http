# framecheck-http build and check targets. Run `make help` for the list.
#
# CXX defaults to `c++`, which is clang++ on macOS and g++ on Debian.
# PY is the Python used for the differential runner and the corpus generator.

CXX ?= c++
PY ?= python3
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc -Iharness
SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g

LIB_SRCS := src/parser.cpp src/chunked.cpp
HARNESS_LIB := harness/protocol.cpp harness/json_lite.cpp
TEST_SRCS := $(wildcard tests/*.cpp)

N ?= 200000
SEED ?= 1
ITERS ?= 5000000
QUICK ?= 0

.PHONY: help all test asan-test oracle-test diff-fuzz fuzz bench threats loc corpus mutants spec-quotes ci clean

help:
	@echo "make test          build and run the unit and corpus tests"
	@echo "make asan-test     the same tests under ASan and UBSan; fails on any report"
	@echo "make diff-fuzz N=200000 SEED=1   differential run against oracle/reference.py"
	@echo "make fuzz ITERS=5000000 SEED=1   mutational fuzzer under ASan and UBSan"
	@echo "make bench         throughput on a pipelined stream (QUICK=1 for smoke mode)"
	@echo "make oracle-test   unit tests of the Python oracle and generator"
	@echo "make threats       check docs/threat-model.md names only tests the binary runs"
	@echo "make loc           count non-test C++ lines"
	@echo "make corpus        regenerate corpus/ from scripts/make_corpus.py"
	@echo "make mutants       apply each mutant in mutants/ to a copy and run the checkers"
	@echo "make spec-quotes RFC_DIR=DIR   check spec quotes against rfc9112.txt and rfc9110.txt"
	@echo "make ci            the fast checks that CI runs"

all: build/harness build/unit_tests build/fuzz_asan build/bench

build:
	mkdir -p build

build/harness: $(LIB_SRCS) $(HARNESS_LIB) harness/main.cpp | build
	$(CXX) $(CXXFLAGS) -O2 -o $@ $(LIB_SRCS) $(HARNESS_LIB) harness/main.cpp

build/unit_tests: $(LIB_SRCS) $(HARNESS_LIB) $(TEST_SRCS) | build
	$(CXX) $(CXXFLAGS) -O1 -g -I. -Itests -o $@ $(LIB_SRCS) $(HARNESS_LIB) $(TEST_SRCS)

build/unit_tests_asan: $(LIB_SRCS) $(HARNESS_LIB) $(TEST_SRCS) | build
	$(CXX) $(CXXFLAGS) $(SAN) -O1 -I. -Itests -o $@ $(LIB_SRCS) $(HARNESS_LIB) $(TEST_SRCS)

build/fuzz_asan: $(LIB_SRCS) fuzz/fuzz.cpp | build
	$(CXX) $(CXXFLAGS) $(SAN) -O1 -o $@ $(LIB_SRCS) fuzz/fuzz.cpp

build/bench: $(LIB_SRCS) bench/bench.cpp | build
	$(CXX) $(CXXFLAGS) -O2 -o $@ $(LIB_SRCS) bench/bench.cpp

test: build/unit_tests
	./build/unit_tests

# The oracle and its generator have their own unit tests. They use only the standard library.
oracle-test:
	cd oracle && $(PY) -I -m unittest discover -s . -p 'test_*.py'

# The grep counts must be zero. Both the test exit status and the counts are checked.
asan-test: build/unit_tests_asan
	@./build/unit_tests_asan > build/asan-test.log 2>&1; status=$$?; \
	hits=$$(grep -c -e 'runtime error' -e 'AddressSanitizer' build/asan-test.log || true); \
	echo "asan-test exit status: $$status"; \
	echo "sanitizer report lines: $$hits"; \
	tail -1 build/asan-test.log; \
	if [ $$status -ne 0 ] || [ "$$hits" -ne 0 ]; then exit 1; fi

build/diff:
	mkdir -p build/diff

# Runs the same seeded cases through the C++ harness and the Python oracle.
diff-fuzz: build/harness | build/diff
	$(PY) diff/run_diff.py --harness build/harness --cases $(N) --seed $(SEED) --out build/diff

# Mutational fuzzing under ASan and UBSan. The fuzzer exits non-zero on any finding.
fuzz: build/fuzz_asan
	./build/fuzz_asan $(ITERS) $(SEED) corpus/valid corpus/smuggling

bench: build/bench
	./build/bench $(if $(filter 1,$(QUICK)),--quick,) \
	  --source-sha256 "$$(sh scripts/source_hash.sh)" \
	  --hardware "$$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)" \
	  --compiler "$$($(CXX) --version | head -1)" > bench/results.json
	@cat bench/results.json

threats: build/unit_tests
	sh scripts/check_threats.sh

loc:
	sh scripts/loc.sh

corpus:
	$(PY) scripts/make_corpus.py

# Each mutant is a patch in mutants/. Every mutant runs in its own copy of the
# project, so this target never changes the working tree. It takes several minutes.
mutants:
	sh scripts/run_mutants.sh .

spec-quotes:
	@test -n "$(RFC_DIR)" || { echo "usage: make spec-quotes RFC_DIR=/path/with/rfc9112.txt"; exit 2; }
	$(PY) -I scripts/check_spec_quotes.py "$(RFC_DIR)"

ci: test asan-test oracle-test threats loc

clean:
	rm -rf build bench/results.json oracle/__pycache__ diff/__pycache__ oracle/__pycache__
