#!/bin/sh
set -eu

usage_output=$(./merge)
expected_usage='Welcome to the merge program.
Usage: ./merge source-binary1 source-binary2 -o output-binary'
[ "$usage_output" = "$expected_usage" ]

./merge bin1 bin2 -o bin3
[ -x bin3 ]

bundle_output=$(./bin3)
expected_bundle='Message from bin1
Message from bin2'
[ "$bundle_output" = "$expected_bundle" ]

if ./merge subject.md bin2 -o invalid-output 2>/dev/null; then
    echo "non-ELF input was accepted" >&2
    exit 1
fi
[ ! -e invalid-output ]

echo "All tests passed."
