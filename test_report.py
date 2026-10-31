import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "desktop"))
from report import read_events, render_report

class Tests(unittest.TestCase):
    def test_report(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "audit.tsv").write_text("10\tRF_RSSI\thz=433920000,rssi=-55.0\nBAD\n11\tMANUAL\tA|B\n", encoding="utf-8")
            events = read_events(folder)
            self.assertEqual(len(events), 2)
            s = render_report(folder)
            self.assertIn("RF_RSSI", s)
            self.assertIn("A\\|B", s)
            self.assertIn("SHA-256", s)
    def test_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(read_events(Path(directory)), [])

if __name__ == "__main__":
    unittest.main()
