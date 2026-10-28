"""File-integrity tests, using protocol text fixtures rather than sensor simulation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2] / "software/capture_serial.py"
spec = importlib.util.spec_from_file_location("capture_serial", MODULE)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_preserves_bytes_fragments_bad_json_and_partial_tail(self):
        raw = b'{"type":"event"}\nnot-json\n{"seq":2}\npartial'
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / "new"
            recorder = capture.Recorder(directory, "unit-test-fixture")
            recorder.feed(raw[:8])
            recorder.feed(raw[8:])
            recorder.close("complete", "unit-test")
            self.assertEqual((directory / "transport.bin").read_bytes(), raw)
            rows = [json.loads(s) for s in (directory / "records.jsonl").read_text().splitlines()]
            self.assertEqual([r["record"] for r in rows], [{"type": "event"}, {"seq": 2}])
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(manifest["invalid_lines"], 1)
            self.assertEqual(manifest["partial_tail_bytes"], 7)
            self.assertEqual(manifest["transport_sha256"], hashlib.sha256(raw).hexdigest())
            with self.assertRaises(FileExistsError):
                capture.Recorder(directory, "unit-test-fixture")

    def test_rejects_nonfinite_json_and_recovers_after_long_line(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / "new"
            recorder = capture.Recorder(directory, "unit-test-fixture")
            recorder.feed(b'{"v":NaN}\n' + b'x' * 9000)
            recorder.feed(b'\n{"ok":true}\n{"seq":1,"seq":2}\n')
            recorder.close("complete", "unit-test")
            manifest = json.loads((directory / "manifest.json").read_text())
            self.assertEqual(manifest["invalid_lines"], 3)
            self.assertEqual(manifest["parsed_records"], 1)


if __name__ == "__main__":
    unittest.main()
