"""Register self-contained Manwise harnesses and run the sibling Boundscope."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boundscope", type=Path, default=ROOT.parent / "boundscope/target/debug/boundscope")
    parser.add_argument("--compiler", default=shutil.which("clang"))
    options = parser.parse_args()
    if not options.compiler or not options.boundscope.is_file():
        parser.error("Build ../boundscope with cargo build --locked, and install Clang first.")
    directory = ROOT / "build" / ("boundscope-" + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True, exist_ok=False)
    headers = ["src/include/manwise.h", "vendor/cjson/cJSON.h"]
    shared = ["src/c/json.c", "src/c/text.c", "vendor/cjson/cJSON.c"]
    limits = {"build_timeout_ms": 60000, "test_timeout_ms": 10000, "output_bytes": 262144}
    common = {"root": str(ROOT), "language": "c", "headers": headers,
              "include_dirs": ["src/include", "vendor/cjson"], "limits": limits}
    request = lambda ident, method, params: json.dumps({"jsonrpc": "2.0", "id": ident, "method": method, "params": params}) + "\n"
    metadata = {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
                "io.modelcontextprotocol/clientInfo": {"name": "boundscope", "version": "1"},
                "io.modelcontextprotocol/clientCapabilities": {}}
    discovery = request(1, "server/discover", {"_meta": metadata}) + request(2, "tools/list", {"_meta": metadata})
    invalid_tool = request(3, "tools/call", {"_meta": metadata, "name": "read_manual", "arguments": {"topic": "../ls"}})
    config = {"schema": "boundscope.targets.v1", "compiler": str(Path(options.compiler).resolve()), "targets": [
        dict(common, id="manwise-core", sources=["tests/c/core.c", *shared], cases=[
            {"id": name, "args": [name], "stdin": ""} for name in ["parser", "text", "boundaries", "fuzz"]
        ]),
        dict(common, id="manwise-protocol", sources=[str(p.relative_to(ROOT)) for p in sorted((ROOT / "src/c").glob("*.c"))] + ["vendor/cjson/cJSON.c"], cases=[
            {"id": "discovery", "args": [], "stdin": discovery},
            {"id": "rejected-inputs", "args": [], "stdin": discovery + invalid_tool + '{"a":1,"a":2}\n'},
            {"id": "fragment-at-eof", "args": [], "stdin": '{"jsonrpc":"2.0","id":1'},
        ]),
    ]}
    path = directory / "targets.json"
    path.write_text(json.dumps(config, indent=2) + "\n")
    results = []
    for target in config["targets"]:
        run = subprocess.run([str(options.boundscope), "--config", str(path), "--store", str(directory / "reports"), "check", target["id"]],
                             capture_output=True, text=True, timeout=120)
        print(run.stderr, end="")
        (directory / (target["id"] + ".json")).write_text(run.stdout)
        if run.returncode:
            print(run.stdout)
            raise SystemExit(run.returncode)
        result = json.loads(run.stdout)
        if result["outcome"] != "no_errors_observed" or result["execution"] != "complete":
            raise SystemExit("Boundscope did not establish a complete clean check")
        results.append(result)
        print(target["id"], result["id"], result["outcome"])
    (directory / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    print("Evidence:", directory)


if __name__ == "__main__":
    main()
