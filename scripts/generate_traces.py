#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT.parent / "outputs" / "riscv-cpu"


def main() -> None:
    if not (CORE / "verify" / "regress.py").is_file():
        raise SystemExit(f"core not found at {CORE}")
    subprocess.run(["python3", "verify/regress.py", "--simulator", "verilator",
                    "--output-dir", str(CORE / "build" / "cache-traces"),
                    "--memory-traces", str(ROOT / "traces")],
                   cwd=CORE, check=True)


if __name__ == "__main__":
    main()
