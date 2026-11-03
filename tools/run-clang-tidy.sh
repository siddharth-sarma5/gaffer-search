#!/usr/bin/env bash
# Runs clang-tidy over our sources using a build dir's compile_commands.json.
# Usage: tools/run-clang-tidy.sh [build-dir]   (default: build/debug)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

BUILD_DIR="${1:-build/debug}"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
  echo "error: $BUILD_DIR/compile_commands.json not found; configure first" >&2
  exit 1
fi

# Only our translation units (src/, apps/). Headers are covered via
# HeaderFilterRegex in .clang-tidy. Tests are skipped: GoogleTest macros trip
# many style checks that don't apply to test code.
mapfile -t files < <(git ls-files 'src/*.cpp' 'apps/*.cpp')
if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi

status=0
for f in "${files[@]}"; do
  "$CLANG_TIDY" -p "$BUILD_DIR" --quiet "$f" || status=1
done
[[ $status -eq 0 ]] && echo "clang-tidy: ${#files[@]} files clean"
exit $status
