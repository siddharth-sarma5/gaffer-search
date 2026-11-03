#!/usr/bin/env bash
# Runs the same cluster as docker-compose.yml, as local processes (no Docker):
# partitions the corpus, starts N gaffer-shard processes and a gaffer-broker,
# and stops them all on Ctrl-C.
#
#   deploy/local-cluster.sh [SHARDS=4] [BROKER_PORT=7000]
#   ./build/release/apps/gaffer search --broker 127.0.0.1:7000 "late winner at anfield"
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

SHARDS="${1:-4}"
BROKER_PORT="${2:-7000}"
BIN="${BIN:-build/release/apps}"
DIR="${DIR:-build/cluster-${SHARDS}}"

"$BIN/gaffer" partition --shards "$SHARDS" --out-dir "$DIR" \
  --corpus data/corpus/pl-history.tsv --corpus data/corpus/pl-2025-26.tsv

pids=()
cleanup() { kill -TERM "${pids[@]}" 2>/dev/null || true; wait 2>/dev/null || true; }
trap cleanup EXIT INT TERM

shard_args=()
for ((i = 0; i < SHARDS; i++)); do
  port=$((BROKER_PORT + 1 + i))
  "$BIN/gaffer-shard" --corpus "$DIR/shard-$i.tsv" --index "$DIR/shard-$i.gaffer" \
    --shard-id "$i" --num-shards "$SHARDS" --host 127.0.0.1 --port "$port" &
  pids+=($!)
  shard_args+=(--shard "127.0.0.1:$port")
done
"$BIN/gaffer-broker" "${shard_args[@]}" --host 127.0.0.1 --port "$BROKER_PORT" --deadline-ms 200 &
pids+=($!)
echo "cluster up: broker on 127.0.0.1:$BROKER_PORT ($SHARDS shards). Ctrl-C to stop." >&2
wait
