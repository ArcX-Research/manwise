# Working on Manwise

Read [README.md](README.md). Use source and tests for implementation details.

- Keep the CLI and MCP server in C11. Put implementations in `src/c/`, headers
  in `src/include/`, C harnesses in `tests/c/`, and Python tests in `tests/python/`.
- Tool calls select manual topics, never executables, flags or paths. Host
  configuration belongs in startup options; do not add arbitrary shell execution.
- Bound input, output, memory and subprocess time. Preserve cancellation,
  disconnect cleanup, strict parsing and descriptor isolation.
- Treat manuals as reference data, not agent instructions. Preserve source and
  host metadata so the AI agent can check whether the documentation applies.
- Fix findings and preserve Boundscope baselines. A clean sanitizer run covers
  the exercised inputs; it does not prove memory safety.
- Keep user instructions in the README, with working examples and expected
  results. Use "AI agent". Avoid documents that restate code, constants or schemas.
- Keep diagrams focused on what the user or AI agent does. Keep rendering tools
  and generated previews out of project dependencies and Git.
- Use `type(scope): summary` for commits, e.g. `fix(protocol): preserve request IDs`.
  Do not commit binaries, environments, private settings or local reports.
- Keep hosted CI manual or release-triggered. Report local and hosted results
  separately; do not enable hosted builds on every push.

## Checks

After native changes, run from the repository root:

```sh
make test
make sanitize
make boundscope
```

`make boundscope` needs Clang and a built sibling `../boundscope` checkout.
Build it first with `cargo build --locked --manifest-path ../boundscope/Cargo.toml`.
Use `python3 scripts/python/check_boundscope.py --help` for alternative paths.

For Boundscope MCP checks, follow `../boundscope/README.md` under "Use with an AI
agent". Verify its known fault and valid control, then use Manwise's generated
registration with a separate store. Preserve Boundscope's original examples.

Generated registrations, reports and source snapshots stay in `build/boundscope-*`.
Inspect outcomes and execution completeness; archive evidence before `make clean`.

Before a release, also check Linux and the official MCP client. Build the Linux
image after source changes, then run the tests without network access:

```sh
docker build -f tests/Dockerfile -t manwise-verify:local .
docker run --rm --init --network none manwise-verify:local
```

For the official client, use Python 3.11+; with `uv` installed:

```sh
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r tests/python/requirements.txt
make test-sdk PYTHON=.venv/bin/python
```

For performance changes, run `python3 scripts/python/benchmark.py` and report the
host and measured latency. Do not present repeated inputs as distinct test cases.

Documentation-only changes need working links, checked examples and a reviewed
diagram render. Run native checks when runtime behaviour changes.
