// SPDX-License-Identifier: GPL-3.0-or-later
// Writes golden/plain-texts.json: plain texts (message bodies, captions,
// thread lines, reaction keys, room names and previews) rendered by the
// JavaScript the QML used before 84311d4, 2754eef and ba68435 moved it to
// C++ (old-render.js, run in QJSEngine), with rustrx.emojify being the
// Twemoji::inHtml of that time (Twemoji.cpp of 84311d4^, which
// make-golden.sh puts beside this file). The corpus is made here: hand
// written edge cases, then generated ones from a fixed seed. No real
// messages.
//
//   gen <out.json>         the golden file
//   gen --raw <out.json>   the same with emojify doing nothing, for a
//                          check of the JavaScript under node

#include "Twemoji.h"

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <cstdio>

namespace {

const QString kDir = QStringLiteral(RUSTRX_ROOT "/qml/twemoji");
// the sizes the test gives the models
const int kEmote = 40, kSmall = 27, kName = 42, kPreview = 33;

bool g_raw = false;

class Emojify : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QString emojify(const QString &html, int height) const
    {
        return g_raw ? html : Twemoji::inHtml(html, height);
    }
};

QString U(const char *s) { return QString::fromUtf8(s); }

// xorshift32: the same corpus on every machine
quint32 g_state = 20261116u;
quint32 next()
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    return g_state;
}
int below(int n) { return int(next() % quint32(n)); }

const QStringList &words()
{
    static const QStringList w = {
        U("hello"), U("Größe"), U("日本語"), U("ok"), U("a"), U("123"), U("x_y"), U("über"), U("ça"),
        U("@alice:example.org"), U("#room:example.org"), U("!id:example.org"), U("$event"), U("100%"),
        U("𝕏"), U("𠀀"), U("עברית"), U("العربية"), U("é"), U("ﬁ"),
    };
    return w;
}
const QStringList &punctuation()
{
    static const QStringList p = {
        U("."), U(","), U("!"), U("?"), U("("), U(")"), U("\""), U("'"), U(":"), U(";"), U("-"), U("*"),
        U("_"), U("`"), U("~"), U(">"), U("<"), U("&"), U("&amp;"), U("&lt;"), U("&#60;"), U("&#x3C;"),
        U("&nbsp;"), U("$1"), U("$&"), U("\\"), U("/"), U("|"), U("…"),
    };
    return p;
}
const QStringList &links()
{
    static const QStringList l = {
        U("http://example.org"), U("https://example.org/path?q=1&r=2#frag"), U("https://matrix.to/#/#room:example.org"),
        U("matrix:r/room:example.org"), U("matrix:u/alice:example.org?action=chat"), U("HTTP://EXAMPLE.ORG"),
        U("example.org"), U("www.example.org"), U("http://"), U("https://"), U("matrix:"), U("mailto:a@example.org"),
        U("ftp://example.org"), U("javascript:alert(1)"), U("http://example.org/\"x"), U("http://[::1]:8080/"),
        U("https://xn--bcher-kva.example/"), U("https://example.org/😀"), U("https://example.org/a<b"),
        U("xhttp://example.org"), U("http://a"), U("https://example.org/$1"), U("matrix:roomid/!x:example.org"),
    };
    return l;
}
const QStringList &emoji()
{
    static const QStringList e = {
        U("😀"), U("❤"), U("❤️"), U("©"), U("©️"), U("👍🏽"), U("👨‍💻"), U("🏳️‍🌈"), U("🇩🇪"), U("🇩🇪🇫🇷"), U("1️⃣"),
        U("#️⃣"), U("👨‍👩‍👧‍👦"), U("👁️‍🗨️"), U("☁️"), U("™"), U("⌚"), U("️"), U("‍"), U("🏴󠁧󠁢󠁥󠁮󠁧󠁿"),
        U("🇫"), U("❤️‍🔥"), U("👨🏻‍🤝‍👨🏽"), U("🫠"), U("🧑‍🧑‍🧒"), U("⭐"), U("✔️"), U("↔️"),
    };
    return e;
}
const QStringList &spaces()
{
    static const QStringList s = {
        U(" "), U("  "), U("\n"), U("\n\n"), U("\t"), U("\r\n"), U(" "), U(" "), U(" "), U(" "),
        U(" "), U(" "), U(" "), U(" "), U("　"), U("﻿"), U("᠎"), U("​"),
        U("\u0085"), U("\u000B"), U("\u000C"), U("\r"),
    };
    return s;
}
const QStringList &markup()
{
    static const QStringList m = {
        U("**bold**"), U("_it_"), U("`code`"), U("```\ncode\n```"), U("> quote"), U("- item"), U("1. item"),
        U("# head"), U("||spoiler||"), U("~~del~~"), U("<b>"), U("</b>"), U("<script>alert(1)</script>"),
        U("<img src=x onerror=alert(1)>"), U("<a href=\"javascript:x\">"), U("<mx-reply>"), U("<span data-mx-spoiler>"),
        U("<font color=\"#ff0000\">"), U("<br>"), U("<!-- c -->"), U("<<x>img src=\"http://e/\">"),
    };
    return m;
}

QString generated(int maxParts)
{
    const QStringList *pools[] = { &words(), &punctuation(), &links(), &emoji(), &spaces(), &markup() };
    // words, links and emoji most often
    static const int weights[] = { 5, 3, 3, 4, 4, 1 };
    QString s;
    const int parts = 1 + below(maxParts);
    for (int i = 0; i < parts; ++i) {
        int r = below(20), k = 0;
        while (r >= weights[k]) { r -= weights[k]; ++k; }
        const QStringList &pool = *pools[k];
        s += pool.at(below(pool.size()));
        // mostly a space between, sometimes nothing
        const int sep = below(6);
        if (sep < 3) s += QLatin1Char(' ');
        else if (sep == 3) s += spaces().at(below(spaces().size()));
    }
    return s;
}

QStringList handWritten()
{
    QStringList h = {
        QString(), U(" "), U("\n"), U("a"), U("plain text"), U("&"), U("<"), U(">"), U("<>&\"'"),
        U("&amp;&lt;&gt;"), U("&#60;script&#62;"), U("<script>alert(1)</script>"),
        U("<img src=\"http://e.org/p.png\">"), U("<a href=\"javascript:alert(1)\">x</a>"),
        U("http://example.org"), U("see http://example.org."), U("(http://example.org)"),
        U("http://example.org, https://example.org!"), U("http://a\nhttp://b"), U("http://a\thttp://b"),
        U("http://a　b"), U("http://a b"), U("http://a​b"), U("http://a᠎b"), U("http://a\u0085b"),
        U("http://a﻿b"), U("http://a b"), U("matrix:r/x:y.z"), U("matrix:"), U("http://"), U("https://"),
        U("HTTP://A.B"), U("example.org"), U("www.example.org"), U("mailto:x@y.z"), U("xhttp://a"),
        U("http://x\"y"), U("http://x\"href=\"javascript:alert(1)"), U("http://a<b>c"), U("http://a&b"),
        U("http://a.b😀"), U("😀http://a.b"), U("https://a.b/$1$&"),
        U("😀"), U("❤"), U("❤️"), U("©"), U("©️"), U("1️⃣"), U("#️⃣"), U("*️⃣"), U("10"), U("🇩🇪🇫🇷🇩"),
        U("👨‍👩‍👧‍👦"), U("👨‍"), U("‍😀"), U("👁️‍🗨️"), U("🏳️‍🌈"), U("👍🏽👍"), U("🏴󠁧󠁢󠁥󠁮󠁧󠁿"),
        U("a️"), U("️"), U("™ ™️"),
        U("a  b\n\nc\t\td"), U("\n\na\n\n"), U("\r\n"), U("        　﻿ "),
        U("**bold** _it_ `code`"), U("> quote\n> more"), U("- a\n- b"), U("||spoiler||"),
        // reaction keys around the 24-character cut, one with an emoji
        // across it (the cut leaves half of it, in both)
        U("abcdefghijklmnopqrstuvw"), U("abcdefghijklmnopqrstuvwx"), U("abcdefghijklmnopqrstuvwxy"),
        U("abcdefghijklmnopqrstuv😀z"), U("abcdefghijklmnopqrstu😀yz"), U("😀😀😀😀😀😀😀😀😀😀😀😀😀"),
        // long
        U("ab ").repeated(800), U("http://example.org/").repeated(100), U("😀👍🏽").repeated(30),
    };
    return h;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (!args.isEmpty() && args.first() == QLatin1String("--raw")) {
        g_raw = true;
        args.removeFirst();
    }
    if (args.size() != 1) {
        fprintf(stderr, "usage: gen [--raw] out.json\n");
        return 2;
    }
    Twemoji::init(kDir);

    QJSEngine js;
    Emojify emojify;
    js.globalObject().setProperty(QStringLiteral("rustrx"), js.newQObject(&emojify));
    QFile src(QStringLiteral(RUSTRX_ROOT "/tests/cpp/golden/gen/old-render.js"));
    if (!src.open(QIODevice::ReadOnly))
        return 1;
    const QJSValue loaded = js.evaluate(QString::fromUtf8(src.readAll()), src.fileName());
    if (loaded.isError()) {
        fprintf(stderr, "%s\n", qPrintable(loaded.toString()));
        return 1;
    }
    auto call = [&](const char *fn, const QJSValueList &a) {
        const QJSValue r = js.globalObject().property(QLatin1String(fn)).call(a);
        if (r.isError()) {
            fprintf(stderr, "%s: %s\n", fn, qPrintable(r.toString()));
            exit(1);
        }
        return r.toString();
    };
    auto obj = [&](const QVariantMap &m) { return js.toScriptValue(m); };

    // one case per kind: the item (or room) the model gets, and the old
    // JavaScript's output for it
    QJsonArray cases;
    auto add = [&](const char *kind, const QVariantMap &in, const QString &out) {
        QJsonObject c;
        c.insert(QStringLiteral("kind"), QLatin1String(kind));
        c.insert(QStringLiteral("in"), QJsonObject::fromVariantMap(in));
        QString o = out;
        c.insert(QStringLiteral("out"), o.replace(kDir, QStringLiteral("@DIR@")));
        cases.append(c);
    };
    const int kinds = 9;
    auto render = [&](int kind, const QString &s, const QString &other) {
        switch (kind) {
        case 0: case 1: {
            const QVariantMap in { { "type", kind ? "notice" : "text" }, { "body", s } };
            add("body", in, call("body", { obj(in), kEmote }));
            break;
        }
        case 2: {
            // an empty display name: the user id instead
            const QVariantMap in { { "type", "emote" }, { "body", s }, { "sender_name", other },
                                   { "sender", "@user:example.org" } };
            add("body", in, call("body", { obj(in), kEmote }));
            break;
        }
        case 3: {
            const QVariantMap in { { "caption", s } };
            add("caption", in, call("caption", { obj(in) }));
            break;
        }
        case 4: {
            const QString sender = other.isEmpty() ? QStringLiteral("Alice <&>") : other;
            // the old JavaScript read latest_sender, the model since protocol 2
            // latest_sender_name: both, so the cases regenerate the same
            const QVariantMap t { { "latest_sender", sender }, { "latest_sender_name", sender }, { "latest_body", s } };
            add("thread", { { "thread", t } }, call("thread", { obj(t), kSmall }));
            break;
        }
        case 5: {
            if (s.startsWith(QLatin1String("mxc://")))
                break;
            const QVariantMap r { { "key", s }, { "count", 1 } };
            add("label", { { "reactions", QVariantList { r } } }, call("label", { obj(r), kSmall }));
            break;
        }
        case 6: {
            const QVariantMap in { { "name", s } };
            add("name", in, call("roomName", { obj(in), kName }));
            break;
        }
        case 7: case 8: {
            const QVariantMap in { { "preview", s } };
            add(kind == 7 ? "preview" : "preview_multi", in, call("preview", { obj(in), kind == 8, kPreview }));
            break;
        }
        }
    };

    // the hand-written ones in every kind
    for (const QString &s : handWritten())
        for (int k = 0; k < kinds; ++k)
            render(k, s, k == 2 ? QStringLiteral("Bob & <Eve> 😀") : QString());
    // generated: each in one kind, in turn; short names besides
    for (int i = 0; i < 3600; ++i) {
        const QString s = generated(i % 10 == 0 ? 24 : 10);
        const QString other = below(4) ? generated(3) : QString();
        render(i % kinds, s, other);
    }

    QFile out(args.first());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 1;
    // one case per line, so a change shows as a readable diff
    out.write("[\n");
    for (int i = 0; i < cases.size(); ++i) {
        out.write(QJsonDocument(cases.at(i).toObject()).toJson(QJsonDocument::Compact));
        out.write(i + 1 < cases.size() ? ",\n" : "\n");
    }
    out.write("]\n");
    fprintf(stderr, "%d cases\n", cases.size());
    return 0;
}

#include "gen.moc"
