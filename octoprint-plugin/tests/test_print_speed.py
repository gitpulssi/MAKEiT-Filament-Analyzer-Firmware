from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE_DIR = ROOT / "octoprint_makeit_filament_analyzer"
PACKAGE_INIT = PACKAGE_DIR / "__init__.py"


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

    for module_name in (
        "conditioning_limit_patch",
        "invalid_temp_continue_patch",
        "print_speed_patch",
    ):
        full_name = package_name + "." + module_name
        path = PACKAGE_DIR / (module_name + ".py")
        spec = importlib.util.spec_from_file_location(full_name, path)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        sys.modules[full_name] = module
        spec.loader.exec_module(module)

    return sys.modules[package_name + ".print_speed_patch"]


m = load_module()


class PrintSpeedTests(unittest.TestCase):
    def test_print_speed_formula(self):
        speed = m._recommended_print_speed_mm_s(
            delivered_flow_mm3_s=20.0,
            line_width_mm=0.8,
            layer_height_mm=0.4,
            safety_pct=90.0,
        )
        self.assertAlmostEqual(speed, 56.25, places=4)

    def test_nozzle_based_defaults(self):
        definition = {"nozzle_diameter_mm": 0.9}
        geometry = m._normalize_print_geometry({}, definition)
        self.assertAlmostEqual(geometry["print_line_width_mm"], 0.9)
        self.assertAlmostEqual(geometry["print_layer_height_mm"], 0.45)
        self.assertAlmostEqual(geometry["print_speed_safety_pct"], 90.0)

    def test_user_selected_geometry(self):
        definition = {"nozzle_diameter_mm": 1.2}
        geometry = m._normalize_print_geometry(
            {
                "print_line_width_mm": 1.3,
                "print_layer_height_mm": 0.5,
                "print_speed_safety_pct": 85,
            },
            definition,
        )
        self.assertAlmostEqual(geometry["print_line_width_mm"], 1.3)
        self.assertAlmostEqual(geometry["print_layer_height_mm"], 0.5)
        self.assertAlmostEqual(geometry["print_speed_safety_pct"], 85.0)

    def test_saved_run_defaults_are_idempotent(self):
        state = {"definition": {"nozzle_diameter_mm": 0.6}}
        m._add_print_geometry_defaults(state)
        first = dict(state["definition"])
        m._add_print_geometry_defaults(state)
        self.assertEqual(state["definition"], first)
        self.assertAlmostEqual(first["print_line_width_mm"], 0.6)
        self.assertAlmostEqual(first["print_layer_height_mm"], 0.3)


if __name__ == "__main__":
    unittest.main()
