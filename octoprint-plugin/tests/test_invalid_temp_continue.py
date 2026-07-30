from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE_DIR = ROOT / "octoprint_makeit_filament_analyzer"
PACKAGE_INIT = PACKAGE_DIR / "__init__.py"
CONDITIONING_MODULE = PACKAGE_DIR / "conditioning_limit_patch.py"
INVALID_TEMP_MODULE = PACKAGE_DIR / "invalid_temp_continue_patch.py"


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

    conditioning_name = package_name + ".conditioning_limit_patch"
    conditioning_spec = importlib.util.spec_from_file_location(
        conditioning_name,
        CONDITIONING_MODULE,
    )
    assert conditioning_spec and conditioning_spec.loader
    conditioning = importlib.util.module_from_spec(conditioning_spec)
    sys.modules[conditioning_name] = conditioning
    conditioning_spec.loader.exec_module(conditioning)

    patch_name = package_name + ".invalid_temp_continue_patch"
    patch_spec = importlib.util.spec_from_file_location(
        patch_name,
        INVALID_TEMP_MODULE,
    )
    assert patch_spec and patch_spec.loader
    patch = importlib.util.module_from_spec(patch_spec)
    sys.modules[patch_name] = patch
    patch_spec.loader.exec_module(patch)
    return patch


m = load_patch_module()


class InvalidTemperaturePolicyTests(unittest.TestCase):
    def test_recover_before_next_row(self):
        self.assertEqual(
            m._invalid_temp_action(
                next_index=3,
                temperature_count=6,
                continue_enabled=True,
                recovery_enabled=True,
                recover_after_invalid=True,
            ),
            ("recover", 3),
        )

    def test_continue_directly_when_recovery_disabled(self):
        self.assertEqual(
            m._invalid_temp_action(
                next_index=3,
                temperature_count=6,
                continue_enabled=True,
                recovery_enabled=False,
                recover_after_invalid=True,
            ),
            ("row", 3),
        )

    def test_final_invalid_row_completes_with_flag(self):
        self.assertEqual(
            m._invalid_temp_action(
                next_index=6,
                temperature_count=6,
                continue_enabled=True,
                recovery_enabled=True,
                recover_after_invalid=True,
            ),
            ("complete", None),
        )

    def test_strict_setting_stops(self):
        self.assertEqual(
            m._invalid_temp_action(
                next_index=3,
                temperature_count=6,
                continue_enabled=False,
                recovery_enabled=True,
                recover_after_invalid=True,
            ),
            ("stop", None),
        )

    def test_setting_boolean_parser(self):
        self.assertTrue(m._setting_bool("yes", False))
        self.assertFalse(m._setting_bool("off", True))
        self.assertTrue(m._setting_bool(None, True))


if __name__ == "__main__":
    unittest.main()
