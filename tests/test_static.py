# SPDX-License-Identifier: GPL-3.0-or-later
"""Static checks over the whole tree: the wiring between the three
languages, names that must agree across files, and the Qt 5.6 traps that
cost builds before. They read the sources the way the compilers and the
QML engine will, without building anything.

Each test says which bug it exists to catch ("Planted-bug:"); it was
trusted only after failing against that bug planted on purpose."""

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def files(*globs):
    out = []
    for g in globs:
        out += sorted(p for p in ROOT.glob(g) if "twemoji" not in p.parts)
    return out


QML = files("qml/**/*.qml")
QML_JS = QML + files("qml/**/*.js")
CPP = files("src/*.cpp", "src/*.h")
RUST = files("core/src/**/*.rs")


def code(path):
    """A file's text without its // comments (a :// in a URL is kept)."""
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        cut = re.search(r"(?<!:)//", line)
        out.append(line[:cut.start()] if cut else line)
    return "\n".join(out)


def snake(name):
    return re.sub(r"(?<!^)([A-Z])", r"_\1", name).lower()


# --- commands: UI -> core ---

def protocol_commands():
    """The command names the core accepts: protocol.rs's enum, snake_case
    (serde tag "cmd", rename_all snake_case)."""
    src = read("core/src/protocol.rs")
    body = src[src.index("pub(crate) enum Command"):]
    body = body[:body.index("\n}\n")]
    return {snake(v) for v in re.findall(r"^    ([A-Z][A-Za-z]+)\b", body, re.M)}


def sent_commands():
    """Every command name the QML and the C++ send, with where."""
    sent = {}
    for p in QML_JS + CPP:
        s = p.read_text(encoding="utf-8")
        names = re.findall(r'\bcmd\s*:\s*"([a-z_]+)"', s)
        names += re.findall(r'\{\s*"cmd"\s*,\s*"([a-z_]+)"', s)
        for n in names:
            sent.setdefault(n, set()).add(p.relative_to(ROOT).as_posix())
    return sent


# accepted by the core but sent by nothing in the UI, each with its reason
UNSENT_ALLOWED = {
    "refresh_rooms": "for tests through tools/test/dev.sh (kept by the 0.4.6 audit)",
}


def test_every_sent_command_exists_in_the_protocol():
    """Planted-bug: rename "leave_room" in RoomDelegate.qml to "leave"; the
    core would answer with an error event that nothing shows."""
    unknown = {n: sorted(w) for n, w in sent_commands().items()
               if n not in protocol_commands()}
    assert not unknown


def test_every_protocol_command_is_sent_or_listed():
    """A command nothing sends is dead code in the core, or a feature whose
    UI went missing. Planted-bug: delete the "mark_read" send in QML."""
    unsent = protocol_commands() - set(sent_commands())
    assert unsent == set(UNSENT_ALLOWED), unsent


def test_the_command_list_is_the_documented_contract():
    """Adding or removing a command is a reviewed act: update this list
    and docs/PROTOCOL.md with it. Planted-bug: add a variant to the enum."""
    doc = read("docs/PROTOCOL.md")
    missing = sorted(n for n in protocol_commands() if f"`{n}`" not in doc)
    assert not missing


# --- events: core -> UI ---

def emitted_events():
    out = {}
    for p in RUST:
        for n in re.findall(r'"ev"\s*:\s*"([a-z_]+)"', p.read_text(encoding="utf-8")):
            out.setdefault(n, set()).add(p.relative_to(ROOT).as_posix())
    return out


def handled_events():
    """The names Rustrx::onEvent dispatches on."""
    return set(re.findall(r'type\s*==\s*QLatin1String\("([a-z_]+)"\)', read("src/Rustrx.cpp")))


def test_every_emitted_event_is_handled():
    """Planted-bug: rename "paginated" in the core; the chat would never
    learn that older messages came."""
    assert not set(emitted_events()) - handled_events()


def test_every_recovery_key_emitter_sends_the_field_the_ui_reads():
    """The dialog reads "recovery_key"; a reset that sent "key" showed an
    empty recovery key. Planted-bug: send "key" in recovery_reset."""
    ui = read("src/Rustrx.cpp")
    assert 'ev.value("recovery_key")' in ui
    emits = [m for f in RUST for m in re.findall(r'json!\(\{"ev": "recovery_key"[^}]*\}', f.read_text())]
    assert emits
    for e in emits:
        assert '"recovery_key": ' in e[len('json!({"ev": "recovery_key"'):], e


def test_every_handled_event_is_emitted():
    """A handler for an event the core never sends is a renamed event or
    dead code. Planted-bug: rename the "rooms" dispatch in Rustrx.cpp."""
    assert not handled_events() - set(emitted_events())


def test_every_event_is_documented():
    """Planted-bug: emit a new event without a row in docs/PROTOCOL.md."""
    doc = read("docs/PROTOCOL.md")
    assert not sorted(n for n in emitted_events() if f"`{n}`" not in doc)


def test_the_protocol_version_agrees():
    """Planted-bug: bump PROTO in consts.rs only."""
    core = re.search(r"pub\(crate\) const PROTO\w*\s*:\s*\w+\s*=\s*(\d+)", read("core/src/consts.rs"))
    ui = re.search(r"\bkProto\s*=\s*(\d+)", read("src/Keys.h"))
    assert core and ui and core.group(1) == ui.group(1)


# --- QML -> C++ ---

def rustrx_members():
    h = read("src/Rustrx.h")
    names = set(re.findall(r"Q_PROPERTY\(\s*[\w:<>]+\s*\*?\s*(\w+)\s", h))
    names |= set(re.findall(r"Q_INVOKABLE\s+[\w:<>]+\s*[*&]?\s*(\w+)\s*\(", h))
    for section in re.findall(r"(?:signals|public slots):(.*?)(?=\n\s*(?:private|protected|public)\b|\Z)", h, re.S):
        names |= set(re.findall(r"\b(\w+)\s*\(", section))
    return names


def test_qml_uses_only_what_rustrx_has():
    """QML reads an undefined property as undefined and calls of an
    undefined method throw only when reached. Planted-bug: rename
    Q_INVOKABLE markRead in Rustrx.h."""
    members = rustrx_members()
    missing = {}
    for p in QML:
        for n in re.findall(r"(?<![\w-])rustrx\.([a-zA-Z_]\w*)", p.read_text(encoding="utf-8")):
            if n not in members:
                missing.setdefault(n, set()).add(p.name)
    assert not missing


def test_core_alias_is_bound_where_used():
    """Remorse callbacks run after their menu is gone, so they keep
    `core = rustrx` in a local. Planted-bug: drop that local in one file."""
    bad = [p.name for p in QML
           if "core.command(" in p.read_text(encoding="utf-8")
           and "core = rustrx" not in p.read_text(encoding="utf-8")]
    assert not bad


# --- names defined in one place ---

def test_tunables_used_are_defined():
    """Planted-bug: rename a property in Tunables.qml; its users would get
    undefined, which multiplies to NaN without an error."""
    defined = set(re.findall(r"\bproperty\s+\w+\s+(\w+)", read("qml/singletons/Tunables.qml")))
    used = set()
    for p in QML:
        used |= set(re.findall(r"\bTunables\.(\w+)", code(p)))
    assert not used - defined


def test_keys_used_are_defined():
    """Planted-bug: rename a constant in Keys.h (the compiler catches it too;
    this keeps the failure readable without the SDK)."""
    defined = set(re.findall(r"\b(k[A-Z]\w*)\s*(?:\[\]|=)", read("src/Keys.h")))
    used = set()
    for p in CPP:
        used |= set(re.findall(r"Keys::(k[A-Z]\w*)", p.read_text(encoding="utf-8")))
    assert not used - defined


def key_value(name):
    m = re.search(name + r'\s*\[\]\s*=\s*"([^"]+)"', read("src/Keys.h"))
    assert m, name
    return m.group(1)


def test_bus_names_agree_with_the_packaging():
    """The app's bus name and the push connector's name are written in the
    D-Bus activation files too. Planted-bug: rename kService in Keys.h."""
    bus, path, iface = key_value("kBusName"), key_value("kObjectPath"), key_value("kInterface")
    assert "Name=" + bus in read("dbus/net.mowerk.harbour_rustrx.service")
    desktop = read("harbour-rustrx.desktop")
    assert "X-Maemo-Service=" + bus in desktop
    assert "X-Maemo-Object-Path=" + path in desktop
    assert "X-Maemo-Method=" + iface + "." in desktop
    connector = re.search(r'kConnectorName\s*=\s*"([^"]+)"', read("src/UnifiedPush.cpp")).group(1)
    assert "Name=" + connector in read("dbus/" + connector + ".service")
    assert (ROOT / "systemd" / key_value("kStandInUnit")).exists()
    assert (ROOT / "systemd" / key_value("kOpenUnit")).exists()
    assert "background" == key_value("kBackgroundFlag")
    assert "/" + key_value("kBackgroundFlag") in read("systemd/harbour-rustrx.service")


# --- translations ---

def test_every_qstrid_has_its_text_directly_above():
    """sailfishapp_i18n_idbased takes the English text from the //% line
    right above a qsTrId; an id used in several places needs it above one
    of them. Without it the id itself shows in the UI. Planted-bug: put a
    blank line between a //% and its only qsTrId."""
    defined, used = set(), {}
    for p in QML + CPP:
        lines = p.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            for tid in re.findall(r'q[st]TrId\("([^"]+)"', line):
                used.setdefault(tid, f"{p.name}:{i + 1}")
                # lupdate takes it from above the statement, which may
                # begin a line or two higher; a blank line ends the search
                for back in lines[max(0, i - 3):i][::-1]:
                    if not back.strip():
                        break
                    if back.strip().startswith("//%"):
                        defined.add(tid)
                        break
    assert not {t: w for t, w in used.items() if t not in defined}


def test_translation_file_is_in_step():
    """translations/harbour-rustrx.ts is what a translator starts from.
    Planted-bug: add a qsTrId without running lupdate."""
    ts = set(re.findall(r'<message id="([^"]+)"', read("translations/harbour-rustrx.ts")))
    used = set()
    for p in QML + CPP:
        s = p.read_text(encoding="utf-8")
        used |= set(re.findall(r'q[st]TrId\("([^"]+)"', s))
    assert not used - ts, sorted(used - ts)[:20]
    assert not ts - used, sorted(ts - used)[:20]


def test_no_property_shares_a_name_with_an_id():
    """An id wins over a property of the same name in its file: a
    `property string age` on the Label with `id: age` made its text the
    Label itself ("Label_QMLTYPE_20...") in every room row (0.8.0-7).
    Planted-bug: give a property the name of an id in the same file."""
    bad = []
    for p in QML:
        s = p.read_text(encoding="utf-8")
        ids = set(re.findall(r"^\s*id:\s*(\w+)", s, re.M))
        # an alias may name the item it points at (property alias button: button)
        props = set(re.findall(r"^\s*(?:readonly\s+)?(?:default\s+)?property\s+(?!alias\b)[\w<>.]+\s+(\w+)", s, re.M))
        bad += ["%s: %s" % (p.name, n) for n in sorted(ids & props)
                # the page's handle on the composer in its list header, a
                # component of its own where the id is the composer's
                if (p.name, n) not in {("RoomPage.qml", "composer"), ("ThreadPage.qml", "composer")}]
    assert not bad, bad


def test_lupdate_takes_every_text():
    """The build runs lupdate itself: a //% it cannot attach to its
    qsTrId (another member before it on the line, a function in its
    arguments) leaves the id without English, and the phone shows the
    raw id (rustrx-se-debug-log, "Members (%n)"). The committed .ts hides
    it. Planted-bug: put `id: x;` before a `text: qsTrId(...)` under a //%."""
    import shutil, subprocess, tempfile
    tool = shutil.which("lupdate-qt5") or shutil.which("lupdate")
    if not tool:
        pytest.skip("no lupdate")
    with tempfile.TemporaryDirectory() as d:
        out = subprocess.run([tool, "qml", "src", "-ts", d + "/x.ts"], cwd=ROOT,
                             capture_output=True, text=True)
    bad = [l for l in (out.stdout + out.stderr).splitlines()
           if "Discarding unconsumed meta data" in l or "has no source" in l]
    assert not bad, bad


def test_counted_texts_have_english_plurals():
    """A text with %n shows the English of the .ts when no translation
    is installed; without its two forms it read "1 members". The .ts
    needs language="en" too, or lrelease drops the second form.
    Planted-bug: empty one numerusform, or remove the language."""
    ts = read("translations/harbour-rustrx.ts")
    assert re.search(r'<TS [^>]*\blanguage="en"', ts)
    counted = set()
    for p in QML + CPP:
        counted |= set(re.findall(r'q[st]TrId\("([^"]+)",\s*[^\s)]', p.read_text(encoding="utf-8")))
    bad = []
    for tid in sorted(counted):
        m = re.search(r'<message id="%s"( numerus="yes")?>(.*?)</message>' % re.escape(tid), ts, re.S)
        forms = re.findall(r"<numerusform>([^<]+)</numerusform>", m.group(2)) if m and m.group(1) else []
        if len(forms) != 2:
            bad.append(tid)
    assert not bad, bad


# --- Qt 5.6 (Sailfish OS) traps ---

ES5_TRAPS = [
    (r"=>", "arrow function"),
    (r"\?\.", "optional chaining"),
    (r"\?\?", "nullish coalescing"),
    (r"`", "template literal"),
    (r"\blet\s", "let"),
]


@pytest.mark.parametrize("pattern,what", ES5_TRAPS)
def test_qml_javascript_is_es5(pattern, what):
    """Qt 5.6's V4 engine is ES5: newer syntax is a parse error on the phone
    that the host's qmllint accepts. Planted-bug: one arrow function."""
    hits = []
    for p in QML_JS:
        for i, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("//")[0] if "//" in line and "://" not in line else line
            if re.search(pattern, code):
                hits.append(f"{p.name}:{i}")
    assert not hits, what


def test_no_default_property_list():
    """Qt 5.6 cannot parse `default property list<T>`. Planted-bug: one."""
    hits = [p.name for p in QML if re.search(r"default\s+property\s+list<", code(p))]
    assert not hits


SILICA_PRIVATE_ALLOWED = {"MainPage.qml", "SettingsPage.qml"}  # TabView paging; see the hints file


def test_silica_private_only_where_allowed():
    """Private Silica API changes without notice between releases.
    Planted-bug: import it in another page."""
    users = {p.name for p in QML if "Sailfish.Silica.private" in p.read_text(encoding="utf-8")}
    assert users <= SILICA_PRIVATE_ALLOWED


# --- the reviewer's map ---

def test_every_source_file_is_in_the_review_hints():
    """review-and-architecture-hints.md is the map a reviewer reads first.
    Planted-bug: add a core module without naming it there."""
    hints = read("review-and-architecture-hints.md")
    missing = []
    for p in RUST + CPP + files("qml/*.qml", "qml/*.js", "qml/components/*.qml", "qml/singletons/*.qml"):
        if p.stem not in hints:
            missing.append(p.relative_to(ROOT).as_posix())
    assert not missing


LICENCE_EXEMPT = {
    "qml/emoji-data.js": "generated from Unicode data, its own licence note",
}


def test_every_source_file_has_a_licence_header():
    """The GPL header block of the other files (or an SPDX line) in the
    first lines. Planted-bug: a new file without one."""
    missing = []
    for p in RUST + CPP + QML_JS + files("tests/*.py", "tools/**/*.sh", "tools/*.py", "core/*.sh"):
        rel = p.relative_to(ROOT).as_posix()
        head = "".join(p.read_text(encoding="utf-8").splitlines(True)[:12])
        if rel not in LICENCE_EXEMPT and "GNU General Public License" not in head \
                and "SPDX-License-Identifier" not in head:
            missing.append(rel)
    assert not missing


def test_qmllint():
    """qmllint parses every QML file (with a newer Qt than the phone's; the
    ES5 and list<T> tests above cover what it lets through). Planted-bug:
    an unbalanced brace in one page."""
    import shutil
    import subprocess
    lint = shutil.which("qmllint-qt5") or shutil.which("qmllint")
    if not lint:
        pytest.skip("no qmllint")
    r = subprocess.run([lint] + [str(p) for p in QML], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


# --- text from other people ---

def test_labels_default_to_plain_text():
    """Silica's Label renders AutoText by default, so a remote display name
    with a tag became StyledText and fetched its <img> (audit I1).
    Planted-bug: remove _defaultLabelFormat from the window."""
    assert re.search(r"_defaultLabelFormat:\s*Text\.PlainText", read("qml/harbour-rustrx.qml"))


def test_one_escaping_helper():
    """One source for escaping remote text into StyledText (markup.js);
    a hand-written copy forgot `>` or a field before (audit I4).
    Planted-bug: an inline .replace(/&/g, "&amp;") in a delegate."""
    copies = [p.name for p in QML_JS
              if p.name != "markup.js" and 'replace(/&/g' in code(p)]
    assert not copies


def test_external_links_go_through_one_gate():
    """Links from messages and servers are handed to other apps only by
    app.openExternal, which refuses file:, data: and the like (audit I3,
    I6, I7). Our own exported file is the one exception.
    Planted-bug: Qt.openUrlExternally(link) in a page."""
    hits = []
    for p in QML:
        for i, line in enumerate(code(p).splitlines(), 1):
            if "Qt.openUrlExternally(" in line:
                hits.append(f"{p.name}:{i}: {line.strip()}")
    allowed = [h for h in hits if h.startswith("harbour-rustrx.qml") and "Qt.openUrlExternally(url)" in h
               or 'Qt.openUrlExternally("file://" + path.split("/").map(encodeURIComponent).join("/"))' in h]
    assert hits == allowed and len(allowed) == 2, hits


# --- audits ---

def test_every_audit_has_its_ledger_and_is_listed():
    """docs/audits: a narrative and a .findings.json per audit, each in the
    index. Planted-bug: add an audit without its ledger."""
    import json
    folder = ROOT / "docs" / "audits"
    index = (folder / "README.md").read_text(encoding="utf-8")
    narratives = [p for p in folder.glob("*.md") if p.name != "README.md"]
    assert narratives
    for p in narratives:
        ledger = p.with_suffix(".findings.json")
        assert ledger.exists(), p.name
        data = json.loads(ledger.read_text(encoding="utf-8"))
        assert data["findings"] and all("id" in f and "status" in f for f in data["findings"])
        assert p.name in index, p.name


def test_dev_hook_takes_only_the_test_commands():
    """The dev D-Bus hook runs core commands for tests: only those on its
    allowlist (src/DevHook.h, audit S4; before, a refused list, audit I9),
    which must be real commands and none that hands over keys or the
    session, ends it, or sends or fetches files. Rustrx::dev checks it
    before anything runs. Planted-bug: export_keys on the allowlist.
    Planted-bug: the check dropped from Rustrx::dev."""
    src = read("src/DevHook.h")
    block = re.search(r"QMap<QString, QStringList> commands\{(.*?)\};", src, re.S)
    assert block
    allowed = set(re.findall(r'\{ "([a-z_]+)", \{', block.group(1)))
    assert allowed == {"call_start", "refresh_rooms"}
    assert allowed <= protocol_commands()
    never = {"export_keys", "import_keys", "logout", "delete_device", "login_password",
             "login_sso", "login_oauth", "login_qr", "qr_check_code", "recovery_recover",
             "recovery_enable", "recovery_reset", "identity_reset", "push_set", "push_remove",
             "send_file", "get_media"}
    assert not allowed & never
    dev = read("src/Rustrx.cpp")
    dev = dev[dev.index("QString Rustrx::dev("):]
    dev = dev[:dev.index("\n}\n")]
    assert "DevHook::refusal(req)" in dev
    assert dev.index("DevHook::refusal(req)") < dev.index("command(req)")


def test_names_live_in_one_place():
    """The bus name, object path and interface are written once in Keys.h
    (and in the packaging files checked above); QML asks rustrx for them.
    The core's app id and Element Call address live in consts.rs.
    Planted-bug: the bus name as a literal in Service.qml."""
    for p in QML_JS:
        text = code(p)
        for literal in (key_value("kBusName"), key_value("kObjectPath"), key_value("kInterface")):
            assert f'"{literal}"' not in text, p.name
    for p in RUST:
        if p.name == "consts.rs":
            continue
        text = code(p)
        assert "net.mowerk" not in text and "call.element.io" not in text, p.name
    assert 'APP_ID: &str = "' + key_value("kInterface") + '"' in read("core/src/consts.rs")


def protocol_fields():
    """Each command's field names, from protocol.rs's enum."""
    src = read("core/src/protocol.rs")
    body = src[src.index("pub(crate) enum Command"):]
    body = body[:body.index("\n}\n")]
    body = re.sub(r"//[^\n]*", "", body)
    out = {}
    for m in re.finditer(r"^    ([A-Z][A-Za-z]+)\s*(\{(.*?)\})?,", body, re.M | re.S):
        out[snake(m.group(1))] = re.findall(r"(?<![:\w])(\w+)\s*:(?!:)", m.group(3) or "")
    return out


def test_every_field_is_documented():
    """A command's row in PROTOCOL.md names all its fields (create_room's
    last three were missing, audit I15). Planted-bug: a new field."""
    rows = [l for l in read("docs/PROTOCOL.md").splitlines() if l.startswith("| `")]
    missing = []
    for cmd, fields in protocol_fields().items():
        row = next((r for r in rows if r.split("|")[1].find(f"`{cmd}`") >= 0), "")
        missing += [f"{cmd}.{f}" for f in fields if f"`{f}" not in row]
    assert not missing


def test_sent_fields_are_the_protocol_fields():
    """Every field the QML and the C++ send with a command is one the
    command has, and none it needs is left out. The audit I15 rename of
    recovery_recover's key to recovery_key reached the core but not the
    Encryption page, so a typed recovery key failed with 'missing field'
    (the author's find, 0.7). Planted-bug: send key again there."""
    fields = protocol_fields()
    optional = {}
    src = read("core/src/protocol.rs")
    body = src[src.index("pub(crate) enum Command"):]
    body = re.sub(r"//[^\n]*", "", body[:body.index("\n}\n")])
    for m in re.finditer(r"^    ([A-Z][A-Za-z]+)\s*\{(.*?)\},", body, re.M | re.S):
        optional[snake(m.group(1))] = set(re.findall(r"(\w+)\s*:\s*Option<", m.group(2)))
    bad = []
    for p in QML_JS + CPP:
        text = code(p)
        sends = [(m.group(1), set(re.findall(r"(\w+)\s*:", m.group(0))) - {"cmd"})
                 for m in re.finditer(r'\{[^{}]*\bcmd\s*:\s*"([a-z_]+)"[^{}]*\}', text)]
        sends += [(m.group(1), set(re.findall(r'\{\s*"(\w+)"', m.group(2))))
                  for m in re.finditer(r'\{\s*\{\s*"cmd"\s*,\s*"([a-z_]+)"\s*\}((?:\s*,\s*\{\s*"\w+"[^{}]*\})*)\s*\}', text)]
        for cmd, sent in sends:
            if cmd not in fields:
                continue
            have = set(fields[cmd])
            required = have - optional.get(cmd, set())
            bad += [f"{p.name}: {cmd}.{f} unknown" for f in sorted(sent - have)]
            # a bare { cmd: ... } gets its fields further on (UserPage's moderate)
            if sent:
                bad += [f"{p.name}: {cmd}.{f} missing" for f in sorted(required - sent)]
    assert not bad


def test_every_choice_box_takes_press_and_drag():
    """Every choice box is HoldComboBox (Silica's ComboBox with the press and
    drag selection of list item menus); the metadata level in Privacy was a
    plain ComboBox since 0.6.0 and had no drag (the author's find, 0.7).
    Planted-bug: make it a ComboBox again."""
    plain = [p.name for p in QML if p.name != "HoldComboBox.qml"
             and re.search(r"^\s*ComboBox\s*\{", code(p), re.M)]
    assert not plain


def test_pages_import_the_components_they_use():
    """A page that uses one of qml/components/ (HoldComboBox, PageButton,
    ...) imports that folder; without it the page does not load on the
    phone, and qmllint does not notice. Planted-bug: drop the components
    import from PrivacyTab.qml."""
    names = {p.stem for p in files("qml/components/*.qml")}
    missing = []
    for p in QML:
        if p.parent.name == "components":
            continue
        text = code(p)
        if re.search(r'^import\s+"(\.\./)*components"', text, re.M):
            continue
        used = sorted(n for n in names if re.search(r"(?<![\w.])" + n + r"\s*\{", text))
        if used:
            missing.append(f"{p.name}: {', '.join(used)}")
    assert not missing


def test_dconf_path_in_one_place():
    """The dconf folder is written once, in names.js; Settings and the
    debug switches (DebugValue) build their keys from it (audit I16).
    Planted-bug: a ConfigurationValue with the full key in a page."""
    hits = [p.name for p in QML_JS if p.name != "names.js" and "/apps/harbour-rustrx" in code(p)]
    assert not hits


def test_element_call_origin_agrees():
    """The core opens Element Call at ELEMENT_CALL_URL; the frame script
    talks only to ORIGIN. A self-hosted Element Call set in one place only
    would break the origin check silently (audit I16).
    Planted-bug: change the host in consts.rs alone."""
    url = re.search(r'ELEMENT_CALL_URL: &str = "(https://[^/"]+)', read("core/src/consts.rs")).group(1)
    origin = re.search(r'const ORIGIN = "([^"]+)"', read("qml/call-bridge.js")).group(1)
    assert url == origin


def test_c_header_matches_the_core():
    """src/rustrx_core.h is kept by hand: every function the core exports
    is declared there, and nothing else. Planted-bug: a #[no_mangle]
    function without its declaration."""
    exported = set(re.findall(r"#\[no_mangle\]\s*pub extern \"C\" fn (\w+)", read("core/src/lib.rs")))
    declared = set(re.findall(r"\b(rustrx_\w+)\s*\(", re.sub(r"/\*.*?\*/", "", read("src/rustrx_core.h"), flags=re.S)))
    assert exported == declared, (exported ^ declared)


def test_command_fields_say_what_they_are():
    """Protocol 2: one wire name per concept (audit I15). A command field
    named only id, url, key or root meant two things before (a room or an
    alias, an mxc or a web address, a reaction or a recovery key).
    Planted-bug: rename recovery_recover's field back to key."""
    vague = {"id", "url", "key", "root"}
    allowed = {("react", "key")}   # a reaction's key is the spec's own name
    bad = [f"{cmd}.{f}" for cmd, fields in protocol_fields().items() for f in fields
           if f in vague and (cmd, f) not in allowed]
    assert not bad


def test_notification_actions_reach_a_method():
    """A notification's button calls a D-Bus method on Rustrx by name; a
    name that is no Q_SCRIPTABLE method does nothing, silently.
    Planted-bug: rename markRoomRead in Rustrx.h."""
    scriptable = set(re.findall(r"Q_SCRIPTABLE\s+\w+\s+(\w+)\s*\(", read("src/Rustrx.h")))
    called = set(re.findall(r'\baction\(\s*[^,()]+,\s*"(\w+)"', code(ROOT / "qml/Service.qml")))
    assert called and not called - scriptable, called - scriptable


QT56_MISSING = [
    (r"\bRegularExpressionValidator\b", "RegularExpressionValidator (Qt 5.14): RegExpValidator"),
    (r"\bQt\.labs\.", "Qt.labs modules the phone may not have"),
]


@pytest.mark.parametrize("pattern,what", QT56_MISSING)
def test_qml_types_the_phone_has(pattern, what):
    """The phone's Qt is 5.6; the host's qmllint knows newer types.
    Planted-bug: a RegularExpressionValidator in a page."""
    hits = [p.name for p in QML if re.search(pattern, code(p))]
    assert not hits, what


def _qml_function(text, name):
    """The body of a QML JavaScript function, up to its closing brace at
    the function's own indent."""
    m = re.search(r"^( *)function " + name + r"\(.*?\{\n(.*?)^\1\}", text, re.S | re.M)
    assert m, name
    return m.group(2)


def test_room_reads_only_what_was_shown():
    """The room page marks read only what was on screen: the scan is gated
    (opened at the unread line, in front, the app active, the room in the
    timeline), receipts leave only through one place, and that place does
    not check the app state, so a receipt waiting as the app goes to the
    cover still goes out. Opening at the newest end before the unread line
    had come marked a room with 30 unread read.
    Planted-bugs: drop `positioned` from readingNow; send from scanRead
    before the gate; check Qt.application in sendReceipt."""
    page = read("qml/pages/RoomPage.qml")
    gate = _qml_function(page, "readingNow")
    for need in ("positioned", "!readHeld", "PageStatus.Active", "inTimeline", "Qt.application.active"):
        assert need in gate, need
    scan = _qml_function(page, "scanRead")
    assert scan.index("if (!readingNow()) return") < scan.index("readTo(")
    send = _qml_function(page, "sendReceipt")
    assert "Qt.application" not in send
    senders = [m.start() for m in re.finditer(r"app\.markRead\(", page)]
    allowed = [page.index(_qml_function(page, f)) for f in ("sendReceipt", "moveLine")]
    assert len(senders) == 3 and all(any(a <= s < a + 900 for a in allowed) for s in senders)



def test_room_pages_back_by_itself_only_a_few_times():
    """Older pages the room page asks for without a finger on the chat (at
    the top as a page lands, the top reached by itself) are capped and
    counted again only from a touch; the unread seek and a jump are capped
    by Tunables. Every page goes through one of those three places, so none
    can run on until the room's beginning by itself (audit C1).
    Planted-bugs: drop the autoPageMax check; reset autoPages per page
    instead of on movementStarted; a fifth paginate_back sender."""
    page = read("qml/pages/RoomPage.qml")
    gate = _qml_function(page, "maybePaginate")
    idle = gate[gate.index("if (!list.moving)"):]
    for need in ("autoPages >= autoPageMax", "PageStatus.Active", "Qt.application.active", "++autoPages"):
        assert need in idle, need
    assert idle.index("autoPages >= autoPageMax") < idle.index("++autoPages") < idle.index("pageSoon.start()")
    assert re.search(r"readonly property int autoPageMax: [1-9]\b", page)
    resets = re.findall(r"^\s*(.*)autoPages = 0", page, re.M)
    assert len(resets) == 2 and any("onMovementStarted" in r for r in resets), resets
    assert "jumpPages >= Tunables.jumpMaxPages" in _qml_function(page, "tryJump")
    assert "seekPages < Tunables.readSeekPages" in _qml_function(page, "position")
    # the only senders: pageSoon (every maybePaginate), the seek, a jump
    senders = [m.start() for m in re.finditer(r'cmd: "paginate_back"', page)]
    places = [page.index("id: pageSoon"), page.index(_qml_function(page, "position")),
              page.index(_qml_function(page, "tryJump"))]
    assert len(senders) == 3 and all(any(p <= s < p + 1500 for p in places) for s in senders)
    assert "moreLater" in page and "page.maybePaginate(true)" in page[page.index("id: moreLater"):][:200]

def _signals(header):
    """The signals a C++ class offers QML: its signals and its NOTIFYs."""
    s = read(header)
    names = set(re.findall(r"NOTIFY\s+(\w+)", s))
    for block in re.findall(r"\bsignals:(.*?)(?:\n\s*(?:public|private|protected)\b|\n};)", s, re.S):
        names |= set(re.findall(r"void\s+(\w+)\s*\(", block))
    return names


def test_connections_handle_signals_of_their_target():
    """A Connections block handles only signals of its target: rustrx, the
    call engine (rustrx.voip, also as VoipPage's page.call), the models
    (rustrx.rooms and the like) and Qt.application. One-line blocks count
    too. Planted-bug: a block inserted in the middle of another one
    (Service.qml 2026-11-19: the call's Connections took over
    onPushNotification, onCallAnswered and five more, and the service
    silently stopped notifying)."""
    model = {"dataChanged", "rowsInserted", "rowsRemoved", "rowsMoved", "modelReset", "layoutChanged",
             "modelAboutToBeReset", "rowsAboutToBeRemoved", "rowsAboutToBeInserted"}
    rooms = _signals("src/RoomModel.h") | model
    timeline = _signals("src/TimelineModel.h") | model
    voip = _signals("src/Voip.h")
    known = {"rustrx": _signals("src/Rustrx.h"), "rustrx.voip": voip, "page.call": voip,
             "rustrx.rooms": rooms, "rustrx.people": rooms, "rustrx.spaces": rooms, "rustrx.all": rooms,
             "rustrx.timeline": timeline, "rustrx.thread": timeline,
             "Qt.application": {"activeChanged", "stateChanged", "aboutToQuit"}}
    bad = []
    seen = set()
    for p in QML:
        lines = p.read_text(encoding="utf-8").split("\n")
        i = 0
        while i < len(lines):
            line = lines[i]
            if re.match(r"\s*Connections\s*\{", line):
                # one line: Connections { target: x; onY: ... }
                if line.count("{") == line.count("}"):
                    m = re.search(r"target:\s*([\w.]+)", line)
                    target = m.group(1) if m else None
                    handlers = [(i + 1, h[0].lower() + h[1:]) for h in re.findall(r"\bon([A-Z]\w*)\s*:", line)]
                    j = i
                else:
                    depth, j, target, handlers = 0, i, None, []
                    while j < len(lines):
                        depth += lines[j].count("{") - lines[j].count("}")
                        if j > i and depth == 1:
                            m = re.match(r"\s*target:\s*([\w.]+)\s*$", lines[j])
                            if m:
                                target = m.group(1)
                            m = re.match(r"\s*on([A-Z]\w*)\s*:", lines[j])
                            if m:
                                handlers.append((j + 1, m.group(1)[0].lower() + m.group(1)[1:]))
                        if depth == 0 and j > i:
                            break
                        j += 1
                if target in known:
                    seen.add(target)
                    for line_no, sig in handlers:
                        if sig not in known[target]:
                            bad.append(f"{p.name}:{line_no} on{sig[0].upper()}{sig[1:]} for {target}")
                i = j
            i += 1
    assert not bad, bad
    # the parser reaches every kind it knows (a parser that finds nothing passes too)
    assert {"rustrx", "rustrx.voip", "page.call", "rustrx.rooms", "Qt.application"} <= seen, seen

def test_every_translation_has_its_source_text():
    """Each message in the .ts carries the English text (the //% line);
    without it the id itself shows on screen. Planted-bug: the //% above
    an element instead of its text: binding (LoginPage's "Other ways to
    log in" showed "rustrx-se-other-ways" since 0.4.7)."""
    ts = read("translations/harbour-rustrx.ts")
    empty = re.findall(r'<message id="([^"]+)"[^>]*>\s*<source>\s*</source>', ts)
    assert not empty, empty[:20]


def test_qml_that_uses_tunables_imports_them():
    """Tunables is a singleton of the Rustrx module: a file that uses it
    imports Rustrx 1.0, or every binding to it fails at run time.
    Planted-bug: VoipPage without the import (its closing moment was a
    ReferenceError on the phone, qmllint does not see it)."""
    missing = [p.name for p in QML if "Tunables." in code(p) and not re.search(r"^import Rustrx 1\.0", read(p.relative_to(ROOT).as_posix()), re.M)]
    assert not missing, missing


def test_pulse_restarts_when_the_call_rule_changes(tmp_path):
    """The spec's %pre and %post, run with sh against a stand-in /etc and
    /run: PulseAudio (which reads the policy rules at start) is restarted
    when the call rule first arrives or its content changed, and never on
    an update that leaves it as it was, nor where the owner kept an edited
    one (rpm writes .rpmnew then). Audit P-RPM. Planted-bug: the old
    scripts (a restart only on the first install)."""
    import subprocess
    spec = read("rpm/harbour-rustrx.spec")
    pre = re.search(r"^%pre\n(.*?)\n\n", spec, re.S | re.M).group(1)
    post = re.search(r"^%post\n(.*?)\n\n", spec, re.S | re.M).group(1)
    (tmp_path / "etc/pulse/xpolicy.conf.d").mkdir(parents=True)
    (tmp_path / "run").mkdir()
    rule = tmp_path / "etc/pulse/xpolicy.conf.d/harbour-rustrx-xpolicy.conf"
    assert "systemctl-user try-restart pulseaudio.service >/dev/null 2>&1" in post

    def script(s):
        s = s.replace("/etc/pulse", f"{tmp_path}/etc/pulse").replace("/run/", f"{tmp_path}/run/")
        return s.replace("systemctl-user try-restart pulseaudio.service >/dev/null 2>&1",
                         "echo try-restart pulseaudio.service")

    def install(content):
        out = subprocess.run(["sh", "-c", script(pre)], capture_output=True, text=True).stdout
        if content is not None:
            rule.write_text(content)
        out += subprocess.run(["sh", "-c", script(post)], capture_output=True, text=True).stdout
        assert not list((tmp_path / "run").iterdir())   # the markers go
        return "try-restart pulseaudio.service" in out

    assert install("rule 1")
    assert not install("rule 1")
    assert install("rule 2")
    assert not install("rule 2")
    assert not install(None)
