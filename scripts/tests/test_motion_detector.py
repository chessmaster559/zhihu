"""Execute the real C++ motion policy without connecting an ESP32."""
import unittest

from native_test_support import run_native_test


class MotionDetectorTests(unittest.TestCase):
    def test_motion_policy(self):
        run_native_test(self, ["main/pet/motion_detector.cc",
                               "scripts/tests/motion_detector_test.cc"], ["main/pet"])


if __name__ == "__main__":
    unittest.main()
