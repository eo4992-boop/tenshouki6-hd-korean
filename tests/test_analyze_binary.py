import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "analyze_binary.py"


def run(*args):
    return subprocess.run(
        [sys.executable, str(SCRIPT), *map(str, args)],
        capture_output=True,
        text=True,
    )


class AnalyzerTests(unittest.TestCase):
    def test_analyzer_is_deterministic(self):
        source = self._tmp_path("sample.bin")
        source.write_bytes(b"ABCD\x00hello world\xff")
        try:
            first = run(source)
            second = run(source)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            first_json = json.loads(first.stdout)
            second_json = json.loads(second.stdout)
            self.assertEqual(first_json, second_json)
            self.assertEqual(first_json["size"], 17)
        finally:
            source.unlink(missing_ok=True)
            source.parent.rmdir()

    def test_analyzer_rejects_missing_file(self):
        source = self._tmp_path("missing.bin")
        try:
            result = run(source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not a regular file", result.stderr)
        finally:
            source.parent.rmdir()

    @staticmethod
    def _tmp_path(filename):
        import tempfile

        directory = Path(tempfile.mkdtemp(prefix="tenshouki6-test-"))
        return directory / filename


if __name__ == "__main__":
    unittest.main()
