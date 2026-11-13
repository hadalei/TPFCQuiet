"""Acceptance-accounting regressions; no service, process, port or hardware access."""
import datetime as dt
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from soak_monitor import check_state, interval_valid


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.state = dict(connected=True, alive=1, healthy=1, mode=2, requested=1,
                          readback=[1, 1], fault=0, verified=1, ready=1,
                          vendor_fresh=True, lid_closed=0, resume_pending=0)
        self.row = dict(mode="2", requested="1", readback1="1", readback2="1",
                        fault="0", healthy="1", verified="1", vendor_fresh="1", demo="0")
        self.start = dt.datetime(2026, 11, 7, 12).timestamp()
        self.rows = [dict(self.row, time=dt.datetime.fromtimestamp(self.start + i).strftime("%Y-%m-%d %H:%M:%S"))
                     for i in range(-2, 31, 2)]

    def test_live_smart(self):
        self.assertEqual(check_state(self.state, self.row, 1, "smart"), ([], False))

    def test_closed_lid_work_is_not_sleep(self):
        state=dict(self.state,lid_closed=1,closed_lid_work=1,control_paused=0)
        self.assertEqual(check_state(state,self.row,1,"smart"),([],False))

    def test_bios_cannot_count_as_smart(self):
        self.assertTrue(check_state(dict(self.state, mode=0), self.row, 1, "smart")[0])

    def test_status_log_disagreement(self):
        self.assertTrue(check_state(self.state, dict(self.row, mode="0"), 1, "smart")[0])

    def test_stale_vendor(self):
        self.assertTrue(check_state(dict(self.state, vendor_fresh=False), self.row, 1, "smart")[0])

    def test_fan_mismatch(self):
        self.assertTrue(check_state(dict(self.state, readback=[1, 128]), self.row, 1, "smart")[0])

    def test_lid_and_late_resume_pause(self):
        row = dict(self.row, mode="0", requested="128")
        for cause in ("lid_closed", "resume_pending"):
            state = dict(self.state, mode=0, requested=128, readback=[128, 128], **{cause: 1})
            self.assertEqual(check_state(state, row, 1, "smart"), ([], True))

    def test_full_interval(self):
        self.assertTrue(interval_valid(self.rows, self.start, self.start + 30, "smart"))

    def test_hidden_bios_interval_excluded(self):
        self.rows[5]["mode"] = "0"
        self.assertFalse(interval_valid(self.rows, self.start, self.start + 30, "smart"))

    def test_missing_samples_excluded(self):
        self.assertFalse(interval_valid([self.rows[0], self.rows[-1]], self.start, self.start + 30, "smart"))

    def test_hidden_vendor_loss_excluded(self):
        self.rows[5]["vendor_fresh"] = "0"
        self.assertFalse(interval_valid(self.rows, self.start, self.start + 30, "smart"))

    def test_demo_cannot_count(self):
        self.rows[5]["demo"] = "1"
        self.assertFalse(interval_valid(self.rows, self.start, self.start + 30, "smart"))


if __name__ == "__main__":
    unittest.main()
