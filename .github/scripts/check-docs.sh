#!/usr/bin/env bash
# Checks the one-file-per-entry layout of the shared docs (docs/decisions/2026-10-15-shared-docs-one-file-per-entry.md):
# file names, and that no two known bugs share a number.
set -euo pipefail
cd "$(dirname "$0")/../.."
shopt -s nullglob # an empty folder (docs/windows-qa after a finished pass) has no entries to check
fail=0

for dir in docs/status docs/decisions docs/windows-qa; do
  for f in "$dir"/*; do
    name="$(basename "$f")"
    if ! [[ "$name" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[a-z0-9-]+\.md$ ]]; then
      echo "::error file=$f::name must be YYYY-MM-DD-<topic>.md (lower case, digits and hyphens)"; fail=1
    fi
  done
done

for f in docs/known-bugs/*; do
  name="$(basename "$f")"
  if ! [[ "$name" =~ ^([0-9]{2,})-[a-z0-9-]+\.md$ ]]; then
    echo "::error file=$f::name must be NN-<topic>.md (lower case, digits and hyphens)"; fail=1; continue
  fi
  num=$((10#${BASH_REMATCH[1]}))
  if ! head -n1 "$f" | grep -q "^# $num\. "; then
    echo "::error file=$f::first line must be '# $num. <title>'"; fail=1
  fi
done

dups="$(ls docs/known-bugs | sed -E 's/^0*([0-9]+)-.*/\1/' | sort -n | uniq -d)"
if [ -n "$dups" ]; then
  for n in $dups; do
    echo "::error::bug number $n is used by more than one file:" $(ls docs/known-bugs | grep -E "^0*$n-")
  done
  echo "Two PRs took the same bug number; renumber the later one's bug to the next free number."
  fail=1
fi

exit "$fail"
