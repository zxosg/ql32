"""Host checks for the hardware runner's cleanup and heap attribution."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location(
    "memory_runner", Path(__file__).resolve().parents[1] / "tools/ql32-memory-test.py")
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


class CleanupTests(unittest.TestCase):
    def run_case(self, final_total=10000):
        console = Mock()
        heap = runner.HeapSample
        samples = [heap(10000, 5000, 10), heap(6000, 4000, 12),
                   heap(9700, 5000, 11), heap(10000, 5000, 10),
                   heap(final_total, 5000, 10)]
        report = []
        with patch.object(runner, "sample", side_effect=[(s, b"") for s in samples]) as sample, \
             patch.object(runner, "start_worker", return_value=(2, b"")), \
             patch.object(runner, "wait_for_metric", return_value=heap(6000, 4000, 12)), \
             patch.object(runner, "cancel_worker", return_value=(b"", "CANCELLED", 2)) as cancel, \
             patch.object(runner, "reap_worker", return_value=(b"", 1)) as reap:
            try:
                runner.run_worker_case(console, "array", "worker.bas", 1, 100,
                                       samples[0], report_cases=report)
            finally:
                self.cancel_calls = cancel.call_args_list
                self.reap_calls = reap.call_args_list
                self.phases = [call.args[2] for call in sample.call_args_list]
                self.report = report

    def test_result_record_recovery_is_separate_from_workload_recovery(self):
        self.run_case()
        self.assertEqual([call.args[1] for call in self.cancel_calls], [2])
        self.assertEqual([call.args[1] for call in self.reap_calls], [2])
        self.assertEqual(self.phases, ["before1", "during1", "cancelled1", "reaped1", "after1"])
        result = self.report[0]["repetitions"][0]
        self.assertEqual(result["task_record_recovery_bytes"], 300)
        self.assertEqual(result["after_cancel"]["total"], 9700)
        self.assertTrue(result["recovered_total"])

    def test_drift_after_record_removal_fails_recovery(self):
        with self.assertRaisesRegex(runner.TestFailure, "did not recover"):
            self.run_case(final_total=9000)
        self.assertFalse(self.report[0]["repetitions"][0]["recovered_total"])


if __name__ == "__main__":
    unittest.main()
