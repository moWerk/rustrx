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


def test_dev_hook_refuses_keys_and_session():
    """The dev D-Bus hook runs core commands for tests; what hands over keys
    or the session, or ends it, is refused there even with its flag file
    (audit I9). Planted-bug: drop export_keys from the refused list."""
    src = read("src/Rustrx.cpp")
    block = re.search(r"static const QStringList refused\{(.*?)\};", src, re.S)
    assert block
    refused = set(re.findall(r'"([a-z_]+)"', block.group(1)))
    must = {"export_keys", "import_keys", "logout", "delete_device", "login_password",
            "login_sso", "login_oauth", "recovery_recover", "recovery_enable", "recovery_reset",
            "identity_reset", "push_set", "push_remove"}
    assert must <= refused
    assert refused <= protocol_commands()


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


def _signals(header):
    """The signals a C++ class offers QML: its signals and its NOTIFYs."""
    s = read(header)
    names = set(re.findall(r"NOTIFY\s+(\w+)", s))
    for block in re.findall(r"\bsignals:(.*?)(?:\n\s*(?:public|private|protected)\b|\n};)", s, re.S):
        names |= set(re.findall(r"void\s+(\w+)\s*\(", block))
    return names


def test_connections_handle_signals_of_their_target():
    """A Connections block for rustrx or rustrx.voip handles only that
    object's signals. Planted-bug: a block inserted in the middle of
    another one (Service.qml 2026-11-19: the call's Connections took over
    onPushNotification, onCallAnswered and five more, and the service
    silently stopped notifying)."""
    known = {"rustrx": _signals("src/Rustrx.h"), "rustrx.voip": _signals("src/Voip.h")}
    bad = []
    for p in QML:
        lines = p.read_text(encoding="utf-8").split("\n")
        i = 0
        while i < len(lines):
            if re.match(r"\s*Connections\s*\{", lines[i]):
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
                    for line, sig in handlers:
                        if sig not in known[target]:
                            bad.append(f"{p.name}:{line} on{sig[0].upper()}{sig[1:]} for {target}")
                i = j
            i += 1
    assert not bad, bad


def test_every_translation_has_its_source_text():
    """Each message in the .ts carries the English text (the //% line);
    without it the id itself shows on screen. Planted-bug: the //% above
    an element instead of its text: binding (LoginPage's "Other ways to
    log in" showed "rustrx-se-other-ways" since 0.4.7)."""
    ts = read("translations/harbour-rustrx.ts")
    empty = re.findall(r'<message id="([^"]+)"[^>]*>\s*<source>\s*</source>', ts)
    assert not empty, empty[:20]
