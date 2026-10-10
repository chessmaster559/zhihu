import unittest

from native_test_support import run_native_test


class RuntimePolicyTests(unittest.TestCase):
    def test_adc_signal_and_volume(self):
        run_native_test(self, ["scripts/tests/adc_pdm_signal_policy_test.cc"], ["main/audio"])

    def test_pet_controller(self):
        run_native_test(self, ["main/pet/pet_controller.cc", "scripts/tests/pet_controller_test.cc"],
                        ["scripts/tests/pet_stubs", "main/pet", "main"])

    def test_pet_idle_policy(self):
        run_native_test(self, ["scripts/tests/pet_idle_policy_test.cc"], ["main/pet"])

    def test_capture_retry(self):
        run_native_test(self, ["scripts/tests/capture_retry_policy_test.cc"], ["main/audio"])

    def test_output_lifecycle(self):
        run_native_test(self, ["scripts/tests/audio_output_lifecycle_test.cc"], ["main/audio"])

    def test_touch_gesture(self):
        run_native_test(self, ["scripts/tests/touch_gesture_gate_test.cc"], ["main/boards/common"])
