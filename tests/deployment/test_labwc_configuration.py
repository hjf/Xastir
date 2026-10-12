#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('labwc_configuration', Path(__file__).resolve().parents[2] / 'scripts/configure-qt-labwc.py')
configuration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(configuration)


class LabwcConfigurationTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.home = Path(self.directory.name)
        self.labwc = self.home / '.config/labwc'
        self.labwc.mkdir(parents=True)
        self.autostart = self.labwc / 'autostart'
        self.rc = self.labwc / 'rc.xml'
        self.autostart.write_text('#!/bin/sh\n/home/hjf/openhamclock/kiosk.sh &\n/usr/local/bin/xastir &\n')
        self.rc.write_text('<labwc_config><!-- preserve --><windowRules><windowRule identifier="xastir"><action name="MoveToOutput" output="HDMI-A-2"/><action name="ToggleFullscreen"/></windowRule></windowRules></labwc_config>')
        self.desktop = self.home / '.config/autostart/xastir.desktop'
        self.desktop.parent.mkdir(parents=True)
        self.desktop.write_text('[Desktop Entry]\nExec=/usr/local/bin/xastir\n')

    def test_preservation_backups_and_repeated_updates(self):
        original = {p: p.read_bytes() for p in (self.rc, self.autostart, self.desktop)}
        configuration.configure(self.home, 'HDMI-A-2')
        startup = self.autostart.read_text()
        self.assertIn('/home/hjf/openhamclock/kiosk.sh &', startup)
        self.assertNotIn('/usr/local/bin/xastir &', startup)
        self.assertEqual(startup.count(configuration.BEGIN), 1)
        self.assertIn('Hidden=true', self.desktop.read_text())
        for path, data in original.items():
            self.assertEqual(next(path.parent.glob(path.name + '*.bak')).read_bytes(), data)
        snapshots = {p: p.read_bytes() for p in original}
        backups = sorted(self.home.rglob('*.bak'))
        configuration.configure(self.home, 'HDMI-A-2')
        self.assertEqual({p: p.read_bytes() for p in original}, snapshots)
        self.assertEqual(sorted(self.home.rglob('*.bak')), backups)
        configuration.configure(self.home, 'HDMI-A-1')
        rules = ET.parse(self.rc).getroot().find('windowRules').findall('windowRule')
        self.assertEqual(len(rules), 2)
        qt = next(rule for rule in rules if rule.get('identifier') == 'xastir-qt')
        self.assertEqual(qt.get('type'), 'normal')
        self.assertEqual(qt.find('action').get('output'), 'HDMI-A-1')
        self.assertEqual(len(qt.findall('action')), 1)
        launcher = (self.home / '.local/bin/xastir-qt').read_text()
        self.assertIn('xastir-qt" --fullscreen "$@"', launcher)

    def test_malformed_autostart_leaves_configuration_untouched(self):
        self.autostart.write_text('#!/bin/sh\n' + configuration.BEGIN + '\nunfinished\n')
        original = {p: p.read_bytes() for p in (self.rc, self.autostart, self.desktop)}
        with self.assertRaises(ValueError):
            configuration.configure(self.home, 'HDMI-A-2')
        self.assertEqual({p: p.read_bytes() for p in original}, original)
        self.assertFalse(list(self.home.rglob('*.bak')))


if __name__ == '__main__':
    unittest.main()
