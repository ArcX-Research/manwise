"""Protocol, real manuals, and process-boundary regressions (standard library only)."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import unittest

BUILD = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build").resolve()
MODERN = {
    "io.modelcontextprotocol/protocolVersion": "2026-07-28",
    "io.modelcontextprotocol/clientInfo": {"name": "manwise-tests", "version": "1"},
    "io.modelcontextprotocol/clientCapabilities": {},
}


class Session:
    def __init__(self, binary="test-server", args=(), env=None, modern=False, pass_fds=()):
        self.process = subprocess.Popen(
            [str(BUILD / binary), *args], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0, env=env, pass_fds=pass_fds,
        )
        self.buffer = b""
        self.counter = 0
        self.modern = modern
        if not modern:
            result = self.call("initialize", {
                "protocolVersion": "2025-11-25", "capabilities": {},
                "clientInfo": {"name": "tests", "version": "1"},
            })
            assert result["result"]["protocolVersion"] == "2025-11-25", result
            self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def send_bytes(self, data):
        while data:
            n = os.write(self.process.stdin.fileno(), data)
            data = data[n:]

    def send(self, message):
        self.send_bytes(json.dumps(message).encode() + b"\n")

    def read(self, timeout=5):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([self.process.stdout], [], [], left)[0]:
                raise AssertionError("Timed out waiting for protocol response")
            block = os.read(self.process.stdout.fileno(), 65536)
            if not block:
                raise AssertionError("Server closed: " + self.process.stderr.read().decode(errors="replace"))
            self.buffer += block
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def request(self, method, params=None):
        self.counter += 1
        params = dict(params or {})
        if self.modern:
            params["_meta"] = MODERN
        self.send({"jsonrpc": "2.0", "id": self.counter, "method": method, "params": params})
        return self.counter

    def call(self, method, params=None):
        identity = self.request(method, params)
        result = self.read()
        assert result["id"] == identity, result
        return result

    def tool(self, name, **args):
        result = self.call("tools/call", {"name": name, "arguments": args})["result"]
        assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
        return result

    def close(self):
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError("Server did not stop after EOF")
        err = self.process.stderr.read().decode(errors="replace")
        self.process.stdout.close()
        self.process.stderr.close()
        assert "Sanitizer" not in err and "runtime error:" not in err, err
        return err

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def wait_pids(directory):
    path = Path(directory) / "pids"
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if path.exists() and len(path.read_text().split()) == 2:
            return [int(p) for p in path.read_text().split()]
        time.sleep(0.01)
    raise AssertionError("Fixture did not start")


def assert_stopped(case, pids):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        live = []
        for pid in pids:
            p = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True)
            if p.stdout.strip() and not p.stdout.strip().startswith("Z"):
                live.append(pid)
        if not live:
            return
        time.sleep(0.02)
    case.fail(f"Fixture processes still running: {live}")


class ProtocolTests(unittest.TestCase):
    def test_discovery_both_protocols(self):
        for modern in (False, True):
            with self.subTest(modern=modern), Session(modern=modern) as s:
                tools = s.call("tools/list")["result"]["tools"]
                self.assertEqual([t["name"] for t in tools], ["search_manuals", "read_manual", "find_in_manual"])
                self.assertTrue(all(t["annotations"]["readOnlyHint"] for t in tools))
                if modern:
                    discovery = s.call("server/discover")["result"]
                    self.assertIn("2026-07-28", discovery["supportedVersions"])
                    self.assertEqual(discovery["_meta"]["io.modelcontextprotocol/serverInfo"]["name"], "manwise")

    def test_version_errors_and_unknown_methods(self):
        with Session(modern=True) as s:
            meta = dict(MODERN, **{"io.modelcontextprotocol/protocolVersion": "2099-01-01"})
            s.send({"jsonrpc": "2.0", "id": 91, "method": "server/discover", "params": {"_meta": meta}})
            self.assertEqual(s.read()["error"]["code"], -32022)
            self.assertEqual(s.call("unknown")["error"]["code"], -32601)
            self.assertEqual(s.call("tools/list", {"cursor": "bad"})["error"]["code"], -32602)

    def test_reject_bad_json_and_recover(self):
        with Session() as s:
            invalid = [b'{', b'{"a":1,"a":2}', b'{"a":"\\u0000"}', b'{"a":01}',
                       b'{"a":1.}', b'{"a":"\xff"}', b'{}{}', b'[' * 40 + b']' * 40,
                       b'{"a":1e999}', b'{}\0']
            for raw in invalid:
                s.send_bytes(raw + b"\n")
                self.assertEqual(s.read()["error"]["code"], -32700, raw)
            s.send_bytes(b'x' * 70000 + b'\n')
            self.assertEqual(s.read()["error"]["code"], -32700)
            self.assertIn("result", s.call("ping"))

    def test_notifications_never_invoke_tools(self):
        with Session() as s:
            s.send({"jsonrpc": "2.0", "method": "tools/call", "params": {
                "name": "read_manual", "arguments": {"topic": "hang"}}})
            self.assertIn("result", s.call("ping"))
            self.assertFalse(s.tool("read_manual", topic="test")["isError"])

    def test_input_validation(self):
        with Session() as s:
            for topic in ["--help", "/etc/passwd", "../ls", "ls;id", "$(id)", "ls\n", "a" * 129, "a b", ""]:
                with self.subTest(topic=topic):
                    self.assertEqual(s.tool("read_manual", topic=topic)["structuredContent"]["code"], "invalid_arguments")
            for fields in [{"limit": 0}, {"limit": 201}, {"limit": 1.5}, {"limit": True},
                           {"start_line": -1}, {"section": "1:2"}, {"section": None},
                           {"refresh": "true"}, {"command": "ls"}]:
                self.assertTrue(s.tool("read_manual", topic="test", **fields)["isError"])
            for query in ["-x", ".*", "a|b", "a;id", "a\nb"]:
                self.assertTrue(s.tool("search_manuals", query=query)["isError"])

    def test_ids_are_echoed_without_rounding(self):
        with Session() as s:
            for identity in ["job-9", "", -1, 9007199254740991]:
                s.send({"jsonrpc": "2.0", "id": identity, "method": "ping"})
                self.assertEqual(s.read()["id"], identity)
            s.send({"jsonrpc": "2.0", "id": 9007199254740993, "method": "ping"})
            self.assertEqual(s.read()["error"]["code"], -32600)
            for spelling in ["1.00000000000000001", "1e-400", "9007199254740991.1"]:
                s.send_bytes(('{"jsonrpc":"2.0","id":' + spelling + ',"method":"ping"}\n').encode())
                self.assertEqual(s.read()["error"]["code"], -32600)

    def test_byte_by_byte_and_multiple_messages(self):
        with Session() as s:
            raw = json.dumps({"jsonrpc": "2.0", "id": "fragment", "method": "ping"}).encode() + b"\n"
            for char in raw:
                s.send_bytes(bytes([char]))
            self.assertEqual(s.read()["id"], "fragment")
            s.send_bytes(b'{"jsonrpc":"2.0","id":91,"method":"ping"}\n' * 20)
            for _ in range(20):
                self.assertEqual(s.read()["id"], 91)


class ManualTests(unittest.TestCase):
    def test_host_selected_manual_directory(self):
        with tempfile.TemporaryDirectory(prefix="manwise manuals ") as d:
            root = Path(d)
            (root / "man1").mkdir()
            (root / "man1/wisedemo.1").write_text('.TH WISEDEMO 1\n.SH NAME\nwisedemo \\- local manual fixture\n.SH OPTIONS\nA documented option.\n')
            with Session(binary="manwise", args=["--manpath", d]) as s:
                result = s.tool("read_manual", topic="wisedemo")["structuredContent"]
                self.assertIn("local manual fixture", result["text"])
                self.assertIn(str(root.resolve()), result["source"])
                self.assertEqual(s.tool("read_manual", topic="man")["structuredContent"]["code"], "not_found")

    def test_read_find_and_pagination(self):
        with Session() as s:
            first = s.tool("read_manual", topic="test", limit=3)["structuredContent"]
            self.assertEqual(first["next_start_line"], 4)
            second = s.tool("read_manual", topic="test", start_line=4)["structuredContent"]
            self.assertTrue(second["cache_hit"])
            self.assertEqual(second["next_start_line"], None)
            matches = s.tool("find_in_manual", topic="test", query="--ALPHA", limit=1, context=0)["structuredContent"]
            self.assertEqual(matches["matches"][0]["line"], 6)
            later = s.tool("find_in_manual", topic="test", query="--ALPHA", start_line=matches["next_start_line"])["structuredContent"]
            self.assertEqual(later["matches"][0]["line"], 8)

    def test_cache_refresh_and_eviction(self):
        with tempfile.TemporaryDirectory() as d, Session(args=["--manpath", d]) as s:
            self.assertEqual(s.tool("read_manual", topic="cached")["structuredContent"]["text"], "render 1\n")
            self.assertTrue(s.tool("read_manual", topic="cached")["structuredContent"]["cache_hit"])
            self.assertEqual(s.tool("read_manual", topic="cached", refresh=True)["structuredContent"]["text"], "render 2\n")
            for i in range(9):
                self.assertFalse(s.tool("read_manual", topic=f"page{i}")["isError"])
            self.assertEqual(s.tool("read_manual", topic="cached")["structuredContent"]["text"], "render 3\n")

    def test_missing_failure_and_search_are_distinct(self):
        with Session() as s:
            self.assertEqual(s.tool("read_manual", topic="missing")["structuredContent"]["code"], "not_found")
            self.assertEqual(s.tool("read_manual", topic="broken")["structuredContent"]["code"], "backend_error")
            self.assertEqual(s.tool("read_manual", topic="placeholder")["structuredContent"]["code"], "backend_error")
            self.assertEqual(s.tool("search_manuals", query="absent")["structuredContent"]["status"], "no_matches")
            self.assertIn("a\\+b\\.c", s.tool("search_manuals", query="a+b.c")["structuredContent"]["text"])

    def test_long_lines_remain_readable(self):
        with Session() as s:
            start, collected = 1, ""
            while start:
                result = s.tool("read_manual", topic="long", start_line=start)["structuredContent"]
                collected += result["text"]
                start = result["next_start_line"]
            self.assertEqual(collected.replace("\n", ""), "x" * 50000)

    def test_environment_is_not_inherited(self):
        env = dict(os.environ, MANOPT="--bad-option", MANPATH="/missing", NROFF="false", SECRET="private", PAGER="false")
        with Session(env=env) as s:
            result = s.tool("read_manual", topic="environment")
            self.assertFalse(result["isError"])
            self.assertIn("PAGER=/bin/cat", result["structuredContent"]["text"])
            self.assertNotIn("private", json.dumps(result))

    def test_real_manual_and_section(self):
        with Session(binary="manwise") as s:
            result = s.tool("read_manual", topic="man", section="1")
            self.assertFalse(result["isError"], result)
            data = result["structuredContent"]
            self.assertIn("NAME", data["text"])
            self.assertIn("man1", data["source"])
            self.assertIn("os", data)
            self.assertNotIn("\b", data["text"])
            found = s.tool("find_in_manual", topic="man", section="1", query="MANPATH")["structuredContent"]
            self.assertGreater(len(found["matches"]), 0)
            missing = s.tool("read_manual", topic="manwise_missing_829382")
            self.assertEqual(missing["structuredContent"]["code"], "not_found")
            search = s.tool("search_manuals", query="printf")
            self.assertFalse(search["isError"], search)


class ProcessTests(unittest.TestCase):
    def test_inherited_descriptors_are_closed(self):
        with tempfile.TemporaryFile() as private:
            with Session(pass_fds=(private.fileno(),)) as s:
                self.assertFalse(s.tool("read_manual", topic="descriptors")["isError"])

    def test_slow_client_cannot_grow_output_queue(self):
        s = Session()
        try:
            raw = b''.join(json.dumps({"jsonrpc": "2.0", "id": i, "method": "tools/list"}).encode() + b'\n' for i in range(400))
            try:
                s.send_bytes(raw)
            except BrokenPipeError:
                pass
            self.assertEqual(s.process.wait(timeout=4), 2)
        finally:
            error = s.close()
        self.assertIn("output queue", error)

    def test_timeout_and_output_limits(self):
        with tempfile.TemporaryDirectory() as d, Session(args=["--timeout-ms", "1500", "--manpath", d]) as s:
            s.request("tools/call", {"name": "read_manual", "arguments": {"topic": "hang"}})
            pids = wait_pids(d)
            self.assertEqual(s.read()["result"]["structuredContent"]["code"], "timeout")
            assert_stopped(self, pids)
            for topic in ["flood", "errflood"]:
                self.assertEqual(s.tool("read_manual", topic=topic)["structuredContent"]["code"], "output_limit")
            self.assertFalse(s.tool("read_manual", topic="test")["isError"])

    def test_cancel_and_busy(self):
        with tempfile.TemporaryDirectory() as d, Session(args=["--manpath", d]) as s:
            s.tool("read_manual", topic="test")
            identity = s.request("tools/call", {"name": "read_manual", "arguments": {"topic": "hang"}})
            pids = wait_pids(d)
            self.assertTrue(s.tool("read_manual", topic="test")["structuredContent"]["cache_hit"])
            self.assertEqual(s.tool("read_manual", topic="other")["structuredContent"]["code"], "busy")
            s.send({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": identity}})
            self.assertIn("result", s.call("ping"))
            assert_stopped(self, pids)
            self.assertFalse(s.tool("read_manual", topic="other")["isError"])

    def test_disconnect_stops_descendants(self):
        with tempfile.TemporaryDirectory() as d:
            s = Session(args=["--manpath", d])
            try:
                s.request("tools/call", {"name": "read_manual", "arguments": {"topic": "hang"}})
                pids = wait_pids(d)
            finally:
                s.close()
            assert_stopped(self, pids)

    def test_termination_stops_descendants(self):
        with tempfile.TemporaryDirectory() as d:
            s = Session(args=["--manpath", d])
            try:
                s.request("tools/call", {"name": "read_manual", "arguments": {"topic": "hang"}})
                pids = wait_pids(d)
                s.process.terminate()
            finally:
                s.close()
            assert_stopped(self, pids)


class CliTests(unittest.TestCase):
    def test_help_version_and_connection(self):
        for flag in ["--help", "--version"]:
            p = subprocess.run([str(BUILD / "manwise"), flag], capture_output=True, text=True, check=True)
            self.assertIn("manwise", p.stdout.lower())
        p = subprocess.run([str(BUILD / "manwise"), "client-config"], capture_output=True, text=True, check=True)
        entry = json.loads(p.stdout)["mcpServers"]["manwise"]
        self.assertEqual(entry["command"], str(BUILD / "manwise"))
        self.assertEqual(entry["args"], ["serve"])

    def test_reject_invalid_startup_options(self):
        for args in [["--manpath", "."], ["--manpath", "/missing-198992"], ["--timeout-ms", "0"], ["--timeout-ms", "10001"], ["--shell", "sh"]]:
            p = subprocess.run([str(BUILD / "manwise"), *args], capture_output=True, timeout=3)
            self.assertEqual(p.returncode, 2, args)


if __name__ == "__main__":
    unittest.main(verbosity=2)
