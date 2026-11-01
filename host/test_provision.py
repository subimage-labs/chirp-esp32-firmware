#!/usr/bin/env python3
"""Offline tests for provisioning settings. Hosts and keys are synthetic."""

import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import provision  # noqa: E402

LOCAL = {"hermes": "gw.example:8642", "key": "test-key"}
VENICE = {"backend": "venice", "veniceKey": "test-venice-key"}


class ReadEnv(unittest.TestCase):
    def read(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "device.env")
            with open(path, "w") as f:
                f.write(text)
            return provision.read_env(path)

    def read_members(self, members):
        return self.read("".join(f'{k}="{v}"\n' for k, v in members.items()))

    def test_implicit_local_requires_no_ssid_and_adds_no_members(self):
        self.assertEqual(self.read_members(LOCAL), LOCAL)

    def test_explicit_local_requires_no_ssid_and_adds_no_members(self):
        members = dict(LOCAL, backend="local")
        self.assertEqual(self.read_members(members), members)

    def test_venice_requires_only_venice_key(self):
        self.assertEqual(self.read_members(VENICE), VENICE)

    def test_explicit_empty_optional_values_are_preserved(self):
        for key in ("ssid", "pass", "profile", "shim", "venice", "veniceKey"):
            for value in ('""', "''", ""):
                with self.subTest(key=key, value=value):
                    text = 'hermes=gw.example:8642\nkey=test-key\n'
                    self.assertEqual(self.read(text + f"{key}={value}\n"),
                                     dict(LOCAL, **{key: ""}))

    def test_venice_preserves_explicit_empty_local_members(self):
        members = dict(VENICE, hermes="", key="", ssid="", shim="")
        self.assertEqual(self.read_members(members), members)

    def test_local_rejects_missing_required_members(self):
        for backend in (None, "local"):
            for key in ("hermes", "key"):
                with self.subTest(backend=backend, key=key):
                    members = dict(LOCAL, ssid="test-network")
                    if backend is not None:
                        members["backend"] = backend
                    del members[key]
                    with self.assertRaises(SystemExit):
                        self.read_members(members)

    def test_local_rejects_empty_required_members(self):
        for backend in (None, "local"):
            for key in ("hermes", "key"):
                with self.subTest(backend=backend, key=key):
                    members = dict(LOCAL, ssid="test-network")
                    if backend is not None:
                        members["backend"] = backend
                    members[key] = ""
                    with self.assertRaises(SystemExit):
                        self.read_members(members)

    def test_venice_rejects_missing_or_empty_venice_key(self):
        for value in (None, ""):
            with self.subTest(value=value):
                members = dict(LOCAL, backend="venice", ssid="test-network")
                if value is not None:
                    members["veniceKey"] = value
                with self.assertRaises(SystemExit):
                    self.read_members(members)

    def test_empty_file_is_rejected(self):
        with self.assertRaises(SystemExit):
            self.read("")

    def test_comments_and_blank_lines_only_are_rejected(self):
        with self.assertRaises(SystemExit):
            self.read("\n  # settings\n \t\n")

    def test_comments_and_blank_lines_are_ignored(self):
        self.assertEqual(self.read('\n # settings\nhermes=gw.example:8642\n'
                                   '\t\nkey=test-key\n# end\n'), LOCAL)

    def test_quoted_and_unquoted_values_and_surrounding_whitespace(self):
        for quote in ('"', "'", ""):
            with self.subTest(quote=quote):
                self.assertEqual(self.read(
                    f' hermes = {quote}gw.example:8642{quote} \n'
                    f' key = {quote}test-key{quote} \n'), LOCAL)

    def test_quoted_values_preserve_internal_spaces_and_equals(self):
        members = dict(LOCAL, profile="test agent", **{"pass": "example=phrase"})
        self.assertEqual(self.read_members(members), members)

    def test_template_placeholders_are_rejected_in_every_field(self):
        for key in provision.FIELDS:
            for quote in ('"', "'", ""):
                with self.subTest(key=key, quote=quote):
                    members = dict(LOCAL, ssid="test-network")
                    if key == "veniceKey":
                        members["backend"] = "venice"
                    text = "".join(f"{k}={v}\n" for k, v in members.items()
                                   if k != key)
                    with self.assertRaises(SystemExit):
                        self.read(text + f"{key}={quote}your-placeholder{quote}\n")


class SettingsMembers(unittest.TestCase):
    def test_all_supplied_members_are_included(self):
        members = dict(LOCAL, backend="local", profile="test-agent",
                       look="00ff", forget="test-agent")
        self.assertEqual(provision.settings_members(members), members)

    def test_empty_strings_are_included_for_every_field(self):
        members = {key: "" for key in provision.FIELDS}
        self.assertEqual(provision.settings_members(members), members)

    def test_none_values_are_omitted(self):
        members = dict(LOCAL, shim="", profile=None, veniceKey=None)
        self.assertEqual(provision.settings_members(members), dict(LOCAL, shim=""))

    def test_other_false_values_are_included(self):
        members = {"vol": 0, "enabled": False, "profile": ""}
        self.assertEqual(provision.settings_members(members), members)

    def test_empty_mapping_adds_no_members(self):
        self.assertEqual(provision.settings_members({}), {})

    def test_all_none_values_produce_no_members(self):
        self.assertEqual(provision.settings_members({key: None for key in provision.FIELDS}), {})


class PickPort(unittest.TestCase):
    def test_existing_preferred_path_is_returned_unchanged(self):
        for path in ("/dev/ttyACM1", "/dev/serial/by-id/test-device", "test-port"):
            with self.subTest(path=path), \
                    mock.patch.object(provision.os.path, "exists", return_value=True), \
                    mock.patch.object(provision.glob, "glob", return_value=["/dev/ttyACM0"]):
                self.assertEqual(provision.pick_port(path), path)

    def test_missing_by_id_path_does_not_fall_back(self):
        with mock.patch.object(provision.os.path, "exists", return_value=False), \
                mock.patch.object(provision.glob, "glob", return_value=["/dev/ttyACM0"]):
            self.assertIsNone(provision.pick_port("/dev/serial/by-id/test-device"))

    def test_missing_explicit_path_does_not_fall_back(self):
        for path in ("/dev/ttyACM1", "/dev/ttyUSB0", "test-port", ""):
            with self.subTest(path=path), \
                    mock.patch.object(provision.os.path, "exists", return_value=False), \
                    mock.patch.object(provision.glob, "glob", return_value=["/dev/ttyACM0"]):
                self.assertIsNone(provision.pick_port(path))

    def test_none_selects_first_sorted_acm_port(self):
        with mock.patch.object(provision.glob, "glob",
                               return_value=["/dev/ttyACM2", "/dev/ttyACM0", "/dev/ttyACM1"]) as glob:
            self.assertEqual(provision.pick_port(None), "/dev/ttyACM0")
        glob.assert_called_once_with("/dev/ttyACM*")

    def test_none_without_acm_ports_returns_none(self):
        with mock.patch.object(provision.glob, "glob", return_value=[]) as glob:
            self.assertIsNone(provision.pick_port(None))
        glob.assert_called_once_with("/dev/ttyACM*")


if __name__ == "__main__":
    unittest.main()
