# SPDX-License-Identifier: GPL-3.0-or-later
"""qml/transcript.js, the chat as plain text for "Select text", run under
node (skipped without it): each item's line, and where each one starts.

Planted-bug: count the newline before an item twice in build() (every
selection after the first message would start a character late)."""

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
NODE = shutil.which("node")
# 2026-11-17 14:02 UTC
TS = 1794924120000


def run(expr):
    src = (ROOT / "qml/transcript.js").read_text(encoding="utf-8").replace(".pragma library", "")
    script = src + "\nprocess.stdout.write(JSON.stringify(%s))" % expr
    env = dict(os.environ, TZ="UTC")
    out = subprocess.run([NODE, "-e", script], capture_output=True, text=True, check=True, env=env)
    return json.loads(out.stdout)


def ev(**kw):
    m = {"kind": "event", "ts": TS, "sender": "@a:b.org", "sender_name": "Ada", "type": "text"}
    m.update(kw)
    return m


CASES = [
    (ev(body="hi"), "[14:02] Ada: hi"),
    (ev(sender_name=None, body="hi"), "[14:02] @a:b.org: hi"),
    (ev(type="emote", body="waves"), "[14:02] * Ada waves"),
    (ev(type="membership", body="Ada joined"), "[14:02] * Ada joined"),
    (ev(type="redacted"), "[14:02] Ada: [removed]"),
    (ev(type="utd"), "[14:02] Ada: [not decrypted]"),
    (ev(type="image", body="cat.jpg", filename="cat.jpg"), "[14:02] Ada: [image: cat.jpg]"),
    (ev(type="image", body="my cat", filename="cat.jpg"), "[14:02] Ada: [image: cat.jpg] my cat"),
    (ev(body="yes", reply_to={"sender_name": "Bo", "body": "ok?\nsecond line"}),
     "> Bo: ok?\n[14:02] Ada: yes"),
    (ev(body="yes", reply_to={"pending": True}), "[14:02] Ada: yes"),
    (ev(body="sure", reactions=[{"key": "👍", "count": 2}, {"key": "😀", "count": 1}]),
     "[14:02] Ada: sure (👍 2, 😀 1)"),
    ({"kind": "date", "ts": TS}, "--- 2026-11-17 ---"),
    ({"kind": "readmarker"}, ""),
]


@pytest.mark.skipif(not NODE, reason="no node")
@pytest.mark.parametrize("item,want", CASES)
def test_item_text(item, want):
    assert run("itemText(%s)" % json.dumps(item)) == want


@pytest.mark.skipif(not NODE, reason="no node")
def test_build_offsets_point_at_each_item():
    items = [{"kind": "date", "ts": TS}, ev(body="one\ntwo"), {"kind": "readmarker"},
             ev(body="three", reply_to={"sender_name": "Bo", "body": "q"})]
    got = run("build(%s)" % json.dumps(items))
    text, starts = got["text"], got["starts"]
    assert starts[2] == -1
    lines = ["--- 2026-11-17 ---", "[14:02] Ada: one\ntwo", "> Bo: q\n[14:02] Ada: three"]
    assert text == "\n".join(lines)
    for i, want in ((0, lines[0]), (1, lines[1]), (3, lines[2])):
        assert text[starts[i]:starts[i] + len(want)] == want
