#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Maintain the Qt launch entry and monitor rule, with backups and atomic writes."""
import argparse
import datetime
from pathlib import Path
import os
import re
import shlex
import shutil
import tempfile
import xml.etree.ElementTree as ET

BEGIN = '# BEGIN Xastir Qt deployment'
END = '# END Xastir Qt deployment'


def write(path, text, mode=None):
    data = text.encode()
    if path.exists() and path.read_bytes() == data:
        if mode is not None:
            path.chmod(mode)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    old_mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    if path.exists():
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        backup = path.with_name(path.name + '.xastir-qt.' + stamp + '.bak')
        shutil.copy2(path, backup)
        print(f'Backup: {backup}')
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
        os.chmod(temporary, mode if mode is not None else old_mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f'Updated: {path}')


def configure(home, output):
    labwc = home / '.config/labwc'
    rc = labwc / 'rc.xml'
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    root = ET.fromstring(rc.read_bytes(), parser) if rc.exists() else ET.Element('labwc_config')
    if root.tag != 'labwc_config':
        raise ValueError('Expected a labwc_config XML root')
    rules = root.find('windowRules')
    if rules is None:
        rules = ET.SubElement(root, 'windowRules')
    matching = [rule for rule in rules.findall('windowRule') if rule.get('identifier') == 'xastir-qt']
    rule = matching[0] if matching else ET.SubElement(rules, 'windowRule')
    for duplicate in matching[1:]:
        rules.remove(duplicate)
    rule.set('identifier', 'xastir-qt')
    rule.set('type', 'normal')  # Leave child dialogs usable on the same monitor.
    rule.set('matchOnce', 'true')
    for action in list(rule.findall('action')):
        if action.get('name') in ('MoveToOutput', 'ToggleFullscreen'):
            rule.remove(action)
    ET.SubElement(rule, 'action', name='MoveToOutput', output=output)
    ET.SubElement(rule, 'action', name='ToggleFullscreen')
    ET.indent(root, space='    ')
    xml = '<?xml version="1.0"?>\n' + ET.tostring(root, encoding='unicode') + '\n'

    launcher = home / '.local/bin/xastir-qt'
    autostart = labwc / 'autostart'
    lines = autostart.read_text().splitlines() if autostart.exists() else ['#!/bin/sh']
    kept, in_block = [], False
    for line in lines:
        if line.strip() == BEGIN:
            if in_block:
                raise ValueError('Nested managed autostart block')
            in_block = True
            continue
        if line.strip() == END:
            if not in_block:
                raise ValueError('Unmatched managed autostart block')
            in_block = False
            continue
        if in_block:
            continue
        words = shlex.split(line, comments=True) if line.strip() else []
        if words and Path(words[0]).name in ('xastir', 'xastir-qt'):
            continue
        kept.append(line)
    if in_block:
        raise ValueError('Unterminated managed autostart block')
    while kept and not kept[-1].strip():
        kept.pop()
    startup = '\n'.join(kept + ['', BEGIN, shlex.quote(str(launcher)) + ' &', END, ''])

    launch_script = '''#!/bin/sh
# Managed by scripts/deploy-qt-labwc.sh. Settings remain in ~/.config/Xastir.
set -eu
umask 077
if [ -r "$HOME/.config/xastir-qt/environment" ]; then
    . "$HOME/.config/xastir-qt/environment"
fi
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
log_dir="$HOME/.cache/xastir-qt"
mkdir -p "$log_dir"
log_file="$log_dir/application.log"
if [ -f "$log_file" ] && [ "$(wc -c < "$log_file")" -gt 5242880 ]; then
    mv -f "$log_file" "$log_file.previous"
fi
exec flock -n "$XDG_RUNTIME_DIR/xastir-qt.lock" \\
    "$HOME/.local/opt/xastir-qt/current/bin/xastir-qt" "$@" >> "$log_file" 2>&1
'''
    desktop = f'''[Desktop Entry]
Type=Application
Name=Xastir Qt
Exec="{launcher}"
Terminal=false
Categories=Network;HamRadio;
StartupNotify=false
'''
    # Validate/prepare every configuration before modifying any existing file.
    write(rc, xml)
    write(launcher, launch_script, 0o755)
    write(home / '.local/share/applications/xastir-qt.desktop', desktop)
    write(autostart, startup, 0o755)
    old_desktop = home / '.config/autostart/xastir.desktop'
    if old_desktop.exists():
        text = old_desktop.read_text()
        if re.search(r'^Hidden=', text, re.MULTILINE):
            text = re.sub(r'^Hidden=.*$', 'Hidden=true', text, flags=re.MULTILINE)
        else:
            text = text.replace('[Desktop Entry]', '[Desktop Entry]\nHidden=true', 1)
        write(old_desktop, text)
    print(f'Qt main window: fullscreen on {output}; starts at next Labwc login.')


if __name__ == '__main__':
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument('--home', type=Path, default=Path.home())
    argument_parser.add_argument('--output', default='HDMI-A-2')
    args = argument_parser.parse_args()
    configure(args.home, args.output)
