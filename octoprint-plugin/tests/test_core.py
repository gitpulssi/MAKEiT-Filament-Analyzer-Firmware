from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "octoprint_makeit_filament_analyzer" / "__init__.py"


def load_module():
    plugin = types.ModuleType("octoprint.plugin")
    for name in (
        "StartupPlugin", "ShutdownPlugin", "SettingsPlugin", "AssetPlugin",
        "TemplatePlugin", "SimpleApiPlugin", "EventHandlerPlugin",
    ):
        setattr(plugin, name, type(name, (), {}))

    octoprint = types.ModuleType("octoprint")
    octoprint.plugin = plugin
    sys.modules["octoprint"] = octoprint
    sys.modules["octoprint.plugin"] = plugin

    flask = types.ModuleType("flask")
    flask.Response = object
    flask.request = types.SimpleNamespace(args={}, script_root="")
    flask.jsonify = lambda *args, **kwargs: {"args": args, "kwargs": kwargs}
    flask.abort = lambda status: (_ for _ in ()).throw(RuntimeError(status))
    flask.send_file = lambda *args, **kwargs: (args, kwargs)
    sys.modules["flask"] = flask

    spec = importlib.util.spec_from_file_location("makeit_fa_test_module", PACKAGE)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m = load_module()


class GridTests(unittest.TestCase):
    def test_inclusive_grid_exact_end(self):
        self.assertEqual(m._inclusive_grid(180, 220, 10, 100), [180, 190, 200, 210, 220])

    def test_inclusive_grid_appends_non_aligned_end(self):
        self.assertEqual(m._inclusive_grid(180, 223, 10, 100), [180, 190, 200, 210, 220, 223])

    def test_grid_rejects_zero_step(self):
        with self.assertRaises(m.ValidationError):
            m._inclusive_grid(180, 220, 0, 100)

    def test_grid_rejects_reverse_range(self):
        with self.assertRaises(m.ValidationError):
            m._inclusive_grid(220, 180, 10, 100)

    def test_grid_limit(self):
        with self.assertRaises(m.ValidationError):
            m._inclusive_grid(0, 100, 1, 20)


class ParserTests(unittest.TestCase):
    def test_parse_fa2(self):
        prefix, fields = m._parse_fields(
            "FA2: result=PASS feed_mm_min=300.00 efficiency_pct=97.08 temp_target=220.00"
        )
        self.assertEqual(prefix, "FA2")
        self.assertEqual(fields["result"], "PASS")
        self.assertEqual(fields["feed_mm_min"], "300.00")
        self.assertEqual(fields["efficiency_pct"], "97.08")

    def test_flow_conversion(self):
        flow = m._flow_mm3_s(600.0, 1.75)
        self.assertAlmostEqual(flow, 24.0528, places=3)


class ClassificationTests(unittest.TestCase):
    def test_accurate(self):
        self.assertEqual(
            m._classify_point(
                result="PASS", tested_mm=200, requested_mm=200,
                efficiency_pct=98, accuracy_threshold_pct=97,
                throughput_threshold_pct=85,
            ),
            "ACCURATE",
        )

    def test_soft_accuracy_loss(self):
        self.assertEqual(
            m._classify_point(
                result="LOW_FEED", tested_mm=200, requested_mm=200,
                efficiency_pct=91, accuracy_threshold_pct=97,
                throughput_threshold_pct=85,
            ),
            "BELOW_ACCURACY_ABOVE_THROUGHPUT",
        )

    def test_hard_failure_below_r(self):
        self.assertEqual(
            m._classify_point(
                result="LOW_FEED", tested_mm=200, requested_mm=200,
                efficiency_pct=84.9, accuracy_threshold_pct=97,
                throughput_threshold_pct=85,
            ),
            "HARD_THROUGHPUT_FAIL",
        )

    def test_hard_failure_on_partial_point(self):
        self.assertEqual(
            m._classify_point(
                result="LOW_FEED", tested_mm=150, requested_mm=200,
                efficiency_pct=90, accuracy_threshold_pct=97,
                throughput_threshold_pct=85,
            ),
            "HARD_THROUGHPUT_FAIL",
        )


if __name__ == "__main__":
    unittest.main()
