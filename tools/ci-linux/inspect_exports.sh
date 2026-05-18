#!/usr/bin/env bash
# Inspect libyse.so exports for issue #34 verification.
set -e

SO=${1:-/workspace/build-linux/bin/libyse.so}

echo "=== total defined exports ==="
nm -D --defined-only "$SO" | wc -l

echo
echo "=== by symbol type ==="
nm -D --defined-only "$SO" | awk '{print $2}' | sort | uniq -c | sort -rn

# Demangle and break down by namespace.
TMP=$(mktemp)
nm -D --defined-only "$SO" | c++filt > "$TMP"

echo
echo "=== T (text) symbols, namespace breakdown ==="
awk '$2=="T"{print $0}' "$TMP" \
  | sed -E 's/^[^ ]+ T //' \
  | sed -E 's/\(.*$//' \
  | sed -E 's/<.*//' \
  | sed -E 's/::[^:]+$//' \
  | sort | uniq -c | sort -rn | head -25

echo
echo "=== INTERNAL:: T-symbols (should be 0 — internal namespace, never tagged) ==="
awk '$2=="T"{print $0}' "$TMP" | grep -c "YSE::INTERNAL::" || true

echo
echo "=== PATCHER:: T-symbols (should be 0 — internal namespace) ==="
awk '$2=="T"{print $0}' "$TMP" | grep -c "YSE::PATCHER::" || true

echo
echo "=== SOUND::Manager / CHANNEL::Manager T-symbols (internal singletons) ==="
awk '$2=="T"{print $0}' "$TMP" | grep -cE "YSE::(SOUND|CHANNEL|REVERB|MIDI|MOTIF|SCALE|PLAYER|DEVICE)::Manager" || true

rm -f "$TMP"
