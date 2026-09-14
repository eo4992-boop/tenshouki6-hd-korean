import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "analyze_binary.py"


def run(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True)


def test_analyzer_is_deterministic(tmp_path):
    source = tmp_path / "sample.bin"
    source.write_bytes(b"ABCD\x00hello world\xff")
    a = run(source)
    b = run(source)
    assert a.returncode == 0
    assert b.returncode == 0
    assert json.loads(a.stdout) == json.loads(b.stdout)
    assert json.loads(a.stdout)["size"] == 17


def test_analyzer_rejects_missing_file(tmp_path):
    result = run(tmp_path / "missing.bin")
    assert result.returncode != 0
