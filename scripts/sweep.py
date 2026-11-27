#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from itertools import product
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SIZES = (1, 2, 4, 8, 16, 32)
WAYS = (1, 2, 4, 8)
BLOCKS = (16, 32, 64, 128)
POLICIES = ("lru", "fifo", "random")
FIELDS = (
    "workload", "size_kib", "ways", "block_bytes", "policy", "fetches",
    "loads", "stores", "instructions", "l1d_accesses", "l1d_hits",
    "l1d_misses", "l1d_miss_rate", "l1d_compulsory", "l1d_capacity",
    "l1d_conflict", "l1d_evictions", "l1d_writebacks", "l1i_misses",
    "l2_misses", "memory_accesses", "access_cycles", "amat", "estimated_cpi",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "cachesim")
    parser.add_argument("--traces", type=Path, default=ROOT / "traces")
    parser.add_argument("--output", type=Path, default=ROOT / "results")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    traces = sorted(args.traces.glob("*.trace"))
    if not traces:
        parser.error("no traces found")
    rows = []
    unique_accesses = 0
    for trace in traces:
        meta = json.loads(trace.with_suffix(".json").read_text())
        unique_accesses += sum(1 for line in trace.read_text().splitlines() if line)
        for size, ways, block, policy in product(SIZES, WAYS, BLOCKS, POLICIES):
            cmd = [str(args.binary), "--trace", str(trace),
                   "--l1i", "8K:2:32", "--l1d", f"{size}K:{ways}:{block}",
                   "--l2", "64K:8:64", "--policy", policy,
                   "--write-policy", "wbwa", "--l1-latency", "1",
                   "--l2-latency", "10", "--memory-latency", "100",
                   "--instructions", str(meta["instructions"]),
                   "--base-cpi", str(meta["base_cpi"]), "--seed", "1"]
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            data = json.loads(result.stdout)
            d = data["l1d"]
            rows.append({
                "workload": trace.stem, "size_kib": size, "ways": ways,
                "block_bytes": block, "policy": policy,
                "fetches": data["fetches"], "loads": data["loads"],
                "stores": data["stores"], "instructions": data["instructions"],
                "l1d_accesses": d["accesses"], "l1d_hits": d["hits"],
                "l1d_misses": d["misses"],
                "l1d_miss_rate": d["misses"] / d["accesses"] if d["accesses"] else 0,
                "l1d_compulsory": d["compulsory"], "l1d_capacity": d["capacity"],
                "l1d_conflict": d["conflict"], "l1d_evictions": d["evictions"],
                "l1d_writebacks": d["writebacks"],
                "l1i_misses": data["l1i"]["misses"],
                "l2_misses": data["l2"]["misses"],
                "memory_accesses": data["memory_accesses"],
                "access_cycles": data["access_cycles"], "amat": data["amat"],
                "estimated_cpi": data["estimated_cpi"],
            })
        print(f"{trace.stem}: {len(SIZES) * len(WAYS) * len(BLOCKS) * len(POLICIES)} configs", flush=True)
    with (args.output / "sweep.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    summary = {
        "traces": len(traces),
        "configs_per_trace": len(SIZES) * len(WAYS) * len(BLOCKS) * len(POLICIES),
        "runs": len(rows),
        "unique_trace_accesses": unique_accesses,
        "simulated_accesses": sum(r["fetches"] + r["loads"] + r["stores"] for r in rows),
        "sizes_kib": SIZES, "ways": WAYS, "blocks_bytes": BLOCKS,
        "policies": POLICIES, "l1i": "8K:2:32", "l2": "64K:8:64",
        "write_policy": "wbwa", "l1_hit_cycles": 1, "l2_hit_cycles": 10,
        "memory_cycles": 100, "seed": 1,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
