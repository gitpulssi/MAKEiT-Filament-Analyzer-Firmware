from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE_DIR = ROOT / "octoprint_makeit_filament_analyzer"
PACKAGE_INIT = PACKAGE_DIR / "__init__.py"
PATCH_MODULE = PACKAGE_DIR / "conditioning_limit_patch.py"


def load_patch_module():
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

    package_name = "octoprint_makeit_filament_analyzer"
    package_spec = importlib.util.spec_from_file_location(
        package_name,
        PACKAGE_INIT,
        submodule_search_locations=[str(PACKAGE_DIR)],
    )
    assert package_spec and package_spec.loader
    package = importlib.util.module_from_spec(package_spec)
    sys.modules[package_name] = package
    package_spec.loader.exec_module(package)

    patch_name = package_name + ".conditioning_limit_patch"
    patch_spec = importlib.util.spec_from_file_location(patch_name, PATCH_MODULE)
    assert patch_spec and patch_spec.loader
    patch = importlib.util.module_from_spec(patch_spec)
    sys.modules[patch_name] = patch
    patch_spec.loader.exec_module(patch)
    return patch


m = load_patch_module()


def example_fields():
    return {
        "tag": "conditioning_limit",
        "state": "LIMIT_FOUND",
        "target": "200.00",
        "temp": "199.39",
        "feed_mm_min": "500.00",
        "conditioning_mm": "50.00",
        "conditioning_events": "24",
        "conditioning_eff": "70.07",
    }


class ConditioningLimitTests(unittest.TestCase):
    def test_conditioning_limit_becomes_hard_failure_cell(self):
        point = m._conditioning_limit_point(
            example_fields(),
            {
                "filament_diameter_mm": 1.75,
                "encoder_events_per_mm": 0.685,
            },
            200.0,
        )

        self.assertEqual(point["result"], "CONDITIONING_LIMIT")
        self.assertEqual(point["classification"], "HARD_THROUGHPUT_FAIL")
        self.assertEqual(point["failure_stage"], "CONDITIONING")
        self.assertFalse(point["measurement_started"])
        self.assertEqual(point["actual_encoder_events"], 24)
        self.assertAlmostEqual(point["expected_encoder_events"], 34.25, places=2)
        self.assertAlmostEqual(point["efficiency_pct"], 70.07, places=2)
        self.assertAlmostEqual(point["commanded_flow_mm3_s"], 20.044, places=3)
        self.assertAlmostEqual(point["delivered_flow_mm3_s"], 14.045, places=3)

    def test_saved_run_backfill_is_idempotent(self):
        state = {
            "definition": {
                "filament_diameter_mm": 1.75,
                "encoder_events_per_mm": 0.685,
            },
            "points": [],
            "rows": [example_fields()],
        }

        self.assertEqual(m._append_conditioning_limit_points(state), 1)
        self.assertEqual(len(state["points"]), 1)
        self.assertEqual(state["points"][0]["result"], "CONDITIONING_LIMIT")

        self.assertEqual(m._append_conditioning_limit_points(state), 0)
        self.assertEqual(len(state["points"]), 1)

    def test_ui_resource_paths_and_template_config(self):
        plugin = m.MakeItFilamentAnalyzerPluginV022()
        asset_folder = pathlib.Path(plugin.get_asset_folder())
        template_folder = pathlib.Path(plugin.get_template_folder())
        configs = plugin.get_template_configs()

        self.assertTrue((asset_folder / "js" / "makeit_filament_analyzer.js").is_file())
        self.assertTrue((asset_folder / "css" / "makeit_filament_analyzer.css").is_file())
        self.assertTrue((template_folder / "makeit_filament_analyzer_tab.jinja2").is_file())
        self.assertEqual(len(configs), 1)
        self.assertEqual(configs[0]["type"], "tab")
        self.assertEqual(configs[0]["template"], "makeit_filament_analyzer_tab.jinja2")
        self.assertTrue(configs[0]["custom_bindings"])

    def test_template_does_not_duplicate_octoprint_tab_wrapper_id(self):
        template = (
            PACKAGE_DIR / "templates" / "makeit_filament_analyzer_tab.jinja2"
        ).read_text(encoding="utf-8")
        self.assertNotIn('id="tab_plugin_makeit_filament_analyzer"', template)
        self.assertIn('data-makeit-fa-ui-version="0.2.2"', template)


if __name__ == "__main__":
    unittest.main()
