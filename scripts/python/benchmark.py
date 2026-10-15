"""Measure actual stdio request latency; includes client JSON encoding/decoding."""
import json
from pathlib import Path
import platform
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/python"))
from test_server import Session  # noqa: E402


def main():
    with Session(binary="manwise") as session:
        uncached = []
        for _ in range(10):
            start = time.perf_counter()
            result = session.tool("read_manual", topic="man", limit=80, refresh=True)
            assert not result["isError"] and not result["structuredContent"]["cache_hit"]
            uncached.append((time.perf_counter() - start) * 1000)
        cached = []
        for _ in range(1000):
            start = time.perf_counter()
            result = session.tool("read_manual", topic="man", limit=80)
            assert not result["isError"] and result["structuredContent"]["cache_hit"]
            cached.append((time.perf_counter() - start) * 1000)
    print(json.dumps({
        "platform": platform.platform(), "binary_bytes": (ROOT / "build/manwise").stat().st_size,
        "manual": "man", "requested_lines": 80,
        "uncached_requests": len(uncached), "uncached_median_ms": statistics.median(uncached),
        "cached_requests": len(cached), "cached_median_ms": statistics.median(cached),
        "cached_p95_ms": sorted(cached)[949],
        "note": "Local stdio latency includes the Python test client; operating-system file caches may be warm."
    }, indent=2))


if __name__ == "__main__":
    main()
