# SPDX-License-Identifier: GPL-3.0-or-later
"""qml/commands.js, the message field's slash commands, run under node
(skipped without it): what each line of text turns into.

Planted-bug: /shrug sent with markdown on (its backslash and
underscores would be eaten)."""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
NODE = shutil.which("node")


def parse(text):
    src = (ROOT / "qml/commands.js").read_text(encoding="utf-8").replace(".pragma library", "")
    script = src + "\nprocess.stdout.write(JSON.stringify(parse(%s)))" % json.dumps(text)
    out = subprocess.run([NODE, "-e", script], capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


CASES = [
    ("hello", {"send": "hello", "markdown": True, "emote": False}),
    ("/me waves", {"send": "waves", "markdown": True, "emote": True}),
    ("/ME waves\nagain", {"send": "waves\nagain", "markdown": True, "emote": True}),
    ("/shrug", {"send": "¯\\_(ツ)_/¯", "markdown": False, "emote": False}),
    ("/shrug ok then", {"send": "ok then ¯\\_(ツ)_/¯", "markdown": False, "emote": False}),
    ("/plain *not bold*", {"send": "*not bold*", "markdown": False, "emote": False}),
    ("/topic A new topic", {"topic": "A new topic"}),
    ("/topic", {"topic": ""}),
    ("/nick Mo", {"nick": "Mo"}),
    ("/invite @a:b.org", {"invite": "@a:b.org"}),
    ("/join #room:b.org", {"join": "#room:b.org"}),
    ("/join !id:b.org", {"join": "!id:b.org"}),
    ("//etc/hosts", {"send": "/etc/hosts", "markdown": True, "emote": False}),
    ("/home", {"unknown": "/home"}),
    ("/me", {"unknown": "/me"}),
    ("/invite bob", {"unknown": "/invite"}),
    ("/join room", {"unknown": "/join"}),
    ("/ not a command", {"send": "/ not a command", "markdown": True, "emote": False}),
    ("a /me in the middle", {"send": "a /me in the middle", "markdown": True, "emote": False}),
]


@pytest.mark.skipif(not NODE, reason="no node")
@pytest.mark.parametrize("text,want", CASES)
def test_slash_commands(text, want):
    assert parse(text) == want
