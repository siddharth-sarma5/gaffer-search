#!/usr/bin/env bash
# Fails if any tracked C++ file is not clang-format clean.
# Usage: tools/check-format.sh [--fix]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
mapfile -t files < <(git ls-files '*.cpp' '*.hpp')
if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi

if [[ "${1:-}" == "--fix" ]]; then
  "$CLANG_FORMAT" -i "${files[@]}"
else
  "$CLANG_FORMAT" --dry-run --Werror "${files[@]}"
  echo "clang-format: ${#files[@]} files clean"
fi
