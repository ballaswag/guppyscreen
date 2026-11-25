"""The simulator keeps app diagnostics and does not turn crashes into success."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


RUNNER = Path(__file__).with_name("run_sim_screen.sh")


class SimulatorOutputTests(unittest.TestCase):
    def test_output_status_and_previous_runs_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "screen.log"
            env = dict(os.environ, SIM_DEBUG="0")
            result = subprocess.run(
                ["bash", str(RUNNER), str(log), "bash", "-c",
                 'echo "app output"; echo "app error" >&2; exit 139'],
                env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 139)
            self.assertIn("app output", log.read_text())
            self.assertIn("app error", log.read_text())
            self.assertIn("screen exited with status 139", log.read_text())
            result = subprocess.run(
                ["bash", str(RUNNER), str(log), "bash", "-c", 'echo "next run"'],
                env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            self.assertIn("app error", log.read_text())
            self.assertIn("next run", log.read_text())
            self.assertIn("screen exited with status 0", log.read_text())


if __name__ == "__main__":
    unittest.main()
