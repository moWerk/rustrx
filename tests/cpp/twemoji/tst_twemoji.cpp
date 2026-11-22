// SPDX-License-Identifier: GPL-3.0-or-later
// Twemoji and StyledText (src/Twemoji.cpp): which characters become
// pictures, and how plain text is made ready for Text.StyledText. Where the
// behaviour is questionable (a bare domain is no link, a link keeps its
// trailing full stop) the test pins what the code does now, so a change is
// a decision and not an accident.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

// the file itself, to reach inText and linkEnd in its unnamed namespace
#include "../../../src/Twemoji.cpp"

#include <QElapsedTimer>
#include <QtTest>

namespace {

const QString kDir = QStringLiteral(RUSTRX_ROOT "/qml/twemoji");

QString U(const char *utf8) { return QString::fromUtf8(utf8); }

QString img(const QString &name, int h = 20)
{
    return QStringLiteral("<img src=\"file://%1/%2.png\" width=\"%3\" height=\"%3\" align=\"middle\">")
            .arg(kDir, name).arg(h);
}

int count(const QString &s, const QString &what) { return s.count(what); }

}

class TestTwemoji : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // without the pictures inHtml returns its input, and every emoji
        // test below would pass for nothing
        Twemoji::init(kDir);
        QVERIFY2(s_names.size() > 3000, "qml/twemoji not found");
    }

    // Planted-bug: init() keeps the ".png" in the names (s_names.insert(f)).
    void initReadsNames()
    {
        QVERIFY(s_names.contains(QStringLiteral("1f600")));
        QVERIFY(s_names.contains(QStringLiteral("1f468-200d-1f4bb")));
        QVERIFY(!s_names.contains(QStringLiteral("1f600.png")));
    }

    // Planted-bug: emojiByDefault's check is dropped in match(), so every
    // single character with a picture is drawn ("1", "©", "❤" as pictures).
    void textPresentationStaysText_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("grinning") << U("😀") << img("1f600");
        QTest::newRow("heart bare") << U("❤") << U("❤");
        QTest::newRow("heart VS16") << U("❤️") << img("2764");
        QTest::newRow("copyright bare") << U("©") << U("©");
        QTest::newRow("copyright VS16") << U("©️") << img("a9");
        QTest::newRow("trade mark bare") << U("™") << U("™");
        QTest::newRow("cloud bare") << U("☁") << U("☁");
        QTest::newRow("cloud VS16") << U("☁️") << img("2601");
        QTest::newRow("watch, emoji by default") << U("⌚") << img("231a");
        QTest::newRow("digit") << U("1") << U("1");
        QTest::newRow("hash") << U("#") << U("#");
        QTest::newRow("lone VS16 stays") << U("a\xEF\xB8\x8F") << U("a\xEF\xB8\x8F");
    }
    void textPresentationStaysText()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(inText(in, 20), out);
        QCOMPARE(Twemoji::inHtml(in, 20), out);
    }

    // Planted-bug: nameOf() always drops U+FE0F (zwj = false), so the
    // pictures filed with their selectors (rainbow flag) are not found.
    void zwjSequences_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("technologist") << U("👨‍💻") << img("1f468-200d-1f4bb");
        QTest::newRow("rainbow flag, filed with FE0F") << U("🏳️‍🌈") << img("1f3f3-fe0f-200d-1f308");
        QTest::newRow("heart on fire") << U("❤️‍🔥") << img("2764-fe0f-200d-1f525");
        // filed as 1f441-200d-1f5e8: found by the bare-name fallback
        QTest::newRow("eye in bubble, filed bare") << U("👁️‍🗨️") << img("1f441-200d-1f5e8");
        QTest::newRow("family of four, 7 code points") << U("👨‍👩‍👧‍👦") << img("1f468-200d-1f469-200d-1f467-200d-1f466");
        QTest::newRow("in text") << U("a👨‍💻b") << U("a") + img("1f468-200d-1f4bb") + U("b");
        // a joiner with nothing after it: the emoji, then the joiner as text
        QTest::newRow("dangling joiner") << U("👨‍") << img("1f468") + U("‍");
    }
    void zwjSequences()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(Twemoji::inHtml(in, 20), out);
    }

    // Planted-bug: match() tries the shortest sequence first (n counts up),
    // so a modifier or the second regional indicator is drawn on its own.
    void skinTonesAndFlags_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("thumbs up, medium") << U("👍🏽") << img("1f44d-1f3fd");
        QTest::newRow("handshake, two tones") << U("👨🏻‍🤝‍👨🏽") << img("1f468-1f3fb-200d-1f91d-200d-1f468-1f3fd");
        QTest::newRow("flag DE") << U("🇩🇪") << img("1f1e9-1f1ea");
        QTest::newRow("two flags") << U("🇩🇪🇫🇷") << img("1f1e9-1f1ea") + img("1f1eb-1f1f7");
        QTest::newRow("England, tag sequence") << U("🏴󠁧󠁢󠁥󠁮󠁧󠁿") << img("1f3f4-e0067-e0062-e0065-e006e-e0067-e007f");
        // three indicators: the first two pair, the third is alone
        QTest::newRow("odd indicators") << U("🇩🇪🇫") << img("1f1e9-1f1ea") + img("1f1eb");
    }
    void skinTonesAndFlags()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(Twemoji::inHtml(in, 20), out);
    }

    // Planted-bug: mayStart() forgets the digits, so keycaps 0-9 stay text.
    void keycaps_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("one") << U("1️⃣") << img("31-20e3");
        QTest::newRow("one without VS16") << U("1⃣") << img("31-20e3");
        QTest::newRow("hash") << U("#️⃣") << img("23-20e3");
        QTest::newRow("star") << U("*️⃣") << img("2a-20e3");
        QTest::newRow("number stays") << U("10") << U("10");
        QTest::newRow("after digits") << U("21️⃣") << U("2") + img("31-20e3");
    }
    void keycaps()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(Twemoji::inHtml(in, 20), out);
    }

    // Planted-bug: the markup expression in inHtml loses its tag branch,
    // so an emoji inside an attribute is replaced and breaks the tag.
    void inHtmlLeavesMarkup_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("between tags") << U("<b>😀</b>") << U("<b>") + img("1f600") + U("</b>");
        QTest::newRow("in an attribute") << U("<a href=\"http://x/😀\">x</a>") << U("<a href=\"http://x/😀\">x</a>");
        QTest::newRow("link text") << U("<a href=\"http://x/😀\">😀</a>") << U("<a href=\"http://x/😀\">") + img("1f600") + U("</a>");
        QTest::newRow("numeric entity stays") << U("&#128512;") << U("&#128512;");
        QTest::newRow("named entity next to emoji") << U("&amp;😀") << U("&amp;") + img("1f600");
        // not a sanitiser: a script tag passes as it is
        QTest::newRow("script passes") << U("<script>😀</script>") << U("<script>") + img("1f600") + U("</script>");
        // a "<" with no ">" after it is text
        QTest::newRow("unclosed lt") << U("a < 😀") << U("a < ") + img("1f600");
        // and one with a ">" later swallows what is between as a tag
        QTest::newRow("lt ... gt is a tag") << U("1 < 😀 > 0") << U("1 < 😀 > 0");
    }
    void inHtmlLeavesMarkup()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(Twemoji::inHtml(in, 20), out);
    }

    // Planted-bug: inText appends a skipped character as QChar(cps[i]),
    // which cuts a character outside the BMP to garbage.
    void otherTextUnchanged_data()
    {
        QTest::addColumn<QString>("in");
        QTest::newRow("empty") << QString();
        QTest::newRow("ascii") << U("Hello, world! 1 + 1 = 2 # * ()");
        QTest::newRow("umlauts and CJK") << U("Größe 日本語 €");
        QTest::newRow("outside the BMP, not emoji") << U("𝕏 𠀀 𓀀");
        QTest::newRow("arrows and dingbats") << U("→ ← ✓ ★");
    }
    void otherTextUnchanged()
    {
        QFETCH(QString, in);
        QCOMPARE(Twemoji::inHtml(in, 20), in);
        QCOMPARE(inText(in, 20), in);
    }

    // Planted-bug: url() takes any match at the start (> 0) instead of the
    // whole string, so "😀😀" gets the first one's picture.
    void url_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        const QString f = QStringLiteral("file://") + kDir + QLatin1Char('/');
        QTest::newRow("emoji") << U("😀") << f + "1f600.png";
        QTest::newRow("zwj") << U("👨‍💻") << f + "1f468-200d-1f4bb.png";
        // a picker cell's text-default character, without its selector
        QTest::newRow("bare heart") << U("❤") << f + "2764.png";
        QTest::newRow("two emoji") << U("😀😀") << QString();
        QTest::newRow("emoji and text") << U("😀a") << QString();
        QTest::newRow("letter") << U("a") << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void url()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(Twemoji::url(in), out);
    }

    // Planted-bug: inText steps one code point after a match (i += 1), so
    // a joined sequence's parts are drawn again after it.
    void longInput()
    {
        const QString unit = U("ab 👨‍👩‍👧‍👦 c 😀\n");
        QString in;
        for (int i = 0; i < 20000; ++i)
            in += unit;
        QElapsedTimer t;
        t.start();
        const QString out = Twemoji::inHtml(in, 20);
        // generous: a quadratic walk would take minutes
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(count(out, QStringLiteral("<img ")), 40000);
        QCOMPARE(count(out, img("1f468-200d-1f469-200d-1f467-200d-1f466")), 20000);
        QCOMPARE(count(out, QStringLiteral("\n")), 20000);
        QVERIFY(!out.contains(img("1f468")));
    }

    // --- StyledText ---

    // Planted-bug: escaped() ignores "greater" and always escapes ">".
    void escaped_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<bool>("greater");
        QTest::addColumn<QString>("out");
        QTest::newRow("empty") << QString() << true << QString();
        QTest::newRow("all three") << U("a<b>&c") << true << U("a&lt;b&gt;&amp;c");
        QTest::newRow("keep greater") << U("a<b>&c") << false << U("a&lt;b>&amp;c");
        QTest::newRow("entity escaped again") << U("&amp;") << true << U("&amp;amp;");
        // quotes are left alone: fine as text, not inside an attribute
        QTest::newRow("quotes stay") << U("\"'") << true << U("\"'");
        QTest::newRow("script") << U("<script>alert(1)</script>") << true << U("&lt;script&gt;alert(1)&lt;/script&gt;");
    }
    void escaped()
    {
        QFETCH(QString, in);
        QFETCH(bool, greater);
        QFETCH(QString, out);
        QCOMPARE(StyledText::escaped(in, greater), out);
    }

    // Planted-bug: linkified() makes "\n" into "<br/>" instead of "<br>".
    void linkified_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        auto a = [](const QString &u) { return QStringLiteral("<a href=\"%1\">%1</a>").arg(u); };
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("http") << U("see http://a.b/c ok") << U("see ") + a("http://a.b/c") + U(" ok");
        QTest::newRow("https") << U("https://a.b") << a("https://a.b");
        QTest::newRow("matrix") << U("matrix:r/x:y.z") << a("matrix:r/x:y.z");
        QTest::newRow("query, & escaped in href") << U("http://a/?x=1&y=2") << a("http://a/?x=1&amp;y=2");
        QTest::newRow("two links") << U("http://a http://b") << a("http://a") + U(" ") + a("http://b");
        QTest::newRow("newline ends link, becomes br") << U("http://a\nb") << a("http://a") + U("<br>b");
        QTest::newRow("only newlines") << U("\n\n") << U("<br><br>");
        // the escaping comes first: a "<" after an address is "&lt;" by then
        // and is taken into the link, as the JavaScript did
        QTest::newRow("lt after link") << U("http://a<b") << a("http://a&lt;b");
        QTest::newRow("in a word") << U("xhttp://a") << U("x") + a("http://a");
    }
    void linkified()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(StyledText::linkified(in), out);
    }

    // Planted-bug: linkEnd returns the end even when nothing follows the
    // scheme, so "http://" alone becomes an empty link.
    void linkifiedEdges_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        auto a = [](const QString &u) { return QStringLiteral("<a href=\"%1\">%1</a>").arg(u); };
        QTest::newRow("scheme alone") << U("http:// and matrix: and https://") << U("http:// and matrix: and https://");
        // bare domains (0.4.8): linked as https, the text as written
        auto bare = [](const QString &d) { return QStringLiteral("<a href=\"https://%1\">%1</a>").arg(d); };
        QTest::newRow("bare domain") << U("example.org") << bare("example.org");
        QTest::newRow("www") << U("www.example.xyz") << bare("www.example.xyz");
        QTest::newRow("www with any tld") << U("www.example.zz") << bare("www.example.zz");
        QTest::newRow("bare with path") << U("see example.org/a?b=1.") << U("see ") + bare("example.org/a?b=1") + U(".");
        QTest::newRow("bare full stop") << U("go to example.org.") << U("go to ") + bare("example.org") + U(".");
        QTest::newRow("bare in brackets") << U("(example.org)") << U("(") + bare("example.org") + U(")");
        QTest::newRow("bare subdomain") << U("matrix.tchncs.de") << bare("matrix.tchncs.de");
        QTest::newRow("room alias not linked") << U("#room:example.org") << U("#room:example.org");
        QTest::newRow("user id not linked") << U("@me:example.org") << U("@me:example.org");
        QTest::newRow("address not linked") << U("me@example.org") << U("me@example.org");
        QTest::newRow("version not linked") << U("v1.2 and 3.14") << U("v1.2 and 3.14");
        QTest::newRow("file not linked") << U("notes.txt") << U("notes.txt");
        QTest::newRow("unknown tld not linked") << U("example.zz") << U("example.zz");
        QTest::newRow("abbreviation not linked") << U("e.g. i.e.") << U("e.g. i.e.");
        QTest::newRow("hyphen edge not linked") << U("-a.com") << U("-a.com");
        QTest::newRow("capital scheme not linked") << U("HTTP://A.B") << U("HTTP://A.B");
        QTest::newRow("mailto not linked") << U("mailto:a@b.c") << U("mailto:a@b.c");
        QTest::newRow("javascript not linked") << U("javascript:alert(1)") << U("javascript:alert(1)");
        // trailing punctuation stays in the link (as in the JavaScript)
        QTest::newRow("full stop") << U("http://a.b/c.") << a("http://a.b/c.");
        QTest::newRow("brackets") << U("(http://a.b)") << U("(") + a("http://a.b)");
        QTest::newRow("comma") << U("http://a.b, x") << a("http://a.b,") + U(" x");
        // an emoji right after an address is part of it
        QTest::newRow("emoji after") << U("http://a.b😀") << a(U("http://a.b😀"));
        QTest::newRow("emoji before") << U("😀http://a.b") << U("😀") + a("http://a.b");
    }
    void linkifiedEdges()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(StyledText::linkified(in), out);
    }

    // Audit finding I3 (2026-11-17), test first: linkified() leaves '"'
    // in an address, which ends the href early; StyledText reads what
    // follows as attributes and takes the last href, so the shown address
    // and the one opened differ. Every link must be one href holding no
    // quote. The I3 rows fail until the fix; the guard row must keep
    // passing.
    // Planted-bug (guard row): the link's tag is written '" >' (a space
    // before ">").
    void auditI3QuoteInPlainLink_data()
    {
        QTest::addColumn<QString>("in");
        QTest::newRow("I3 double quote") << U("https://a\"href=\"file:///p\"");
        QTest::newRow("I3 quote at the end") << U("see https://a/\" ok");
        QTest::newRow("guard: plain link") << U("see https://a/b?c=1&d=2 ok");
    }
    void auditI3QuoteInPlainLink()
    {
        QFETCH(QString, in);
        const QString out = StyledText::linkified(in);
        const int at = out.indexOf(QLatin1String("<a "));
        QVERIFY(at >= 0);
        static const QRegularExpression one(QStringLiteral("^<a href=\"https://[^\"'\\s<>]*\">"));
        QVERIFY2(one.match(out.mid(at)).hasMatch(), qPrintable(out));
    }

    // Planted-bug: isSpace() forgets U+FEFF, which JavaScript's \s has.
    void whiteSpaceTable_data()
    {
        QTest::addColumn<uint>("cp");
        QTest::addColumn<bool>("space");
        for (uint c : { 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x20u, 0xA0u, 0x1680u, 0x2000u, 0x2005u, 0x200Au,
                        0x2028u, 0x2029u, 0x202Fu, 0x205Fu, 0x3000u, 0xFEFFu })
            QTest::newRow(qPrintable(QString::number(c, 16))) << c << true;
        // not white space for JavaScript (U+180E was until Unicode 6.3)
        for (uint c : { 0x85u, 0x180Eu, 0x200Bu, 0x200Cu, 0x200Du, 0x2060u, 0x1Fu, 0x0u })
            QTest::newRow(qPrintable(QString::number(c, 16))) << c << false;
    }
    void whiteSpaceTable()
    {
        QFETCH(uint, cp);
        QFETCH(bool, space);
        // all in the BMP; fromUcs4 would drop U+FEFF as a byte order mark
        const QString ch = QString(QChar(ushort(cp)));
        // a space ends a link, and oneLine makes it " "
        const QString link = StyledText::linkified(QStringLiteral("http://a") + ch + QStringLiteral("b"));
        QCOMPARE(link.startsWith(QStringLiteral("<a href=\"http://a\">")), space);
        QCOMPARE(StyledText::oneLine(QStringLiteral("a") + ch + QStringLiteral("b")),
                 space ? QStringLiteral("a b") : QStringLiteral("a") + ch + QStringLiteral("b"));
    }

    // Planted-bug: oneLine() never marks it is in a run of space, so every
    // space character becomes its own " ".
    void oneLine_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("runs") << U("a \n\t b") << U("a b");
        QTest::newRow("ends") << U("\n\na\n\n") << U(" a ");
        QTest::newRow("only space") << U(" 　 ") << U(" ");
        QTest::newRow("no space") << U("abc") << U("abc");
    }
    void oneLine()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(StyledText::oneLine(in), out);
    }

    // The names are read off the way to the window (0.8.2, ST14): start()
    // hands them to a worker thread, and the first lookup waits for them.
    // Planted-bug: url() or inHtml() without loadNames() (no picture until
    // something else loaded the names), or start() that never loads.
    void lazyNames()
    {
        s_names.clear();
        s_loaded.storeRelease(0);
        QCOMPARE(Twemoji::url(U("😀")), QStringLiteral("file://%1/1f600.png").arg(kDir));
        s_names.clear();
        s_loaded.storeRelease(0);
        QCOMPARE(Twemoji::inHtml(U("😀"), 20), img("1f600"));
        Twemoji::start(kDir);
        QCOMPARE(Twemoji::inHtml(U("😀"), 20), img("1f600"));
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QVERIFY(s_names.size() > 3000);
    }
};

QTEST_GUILESS_MAIN(TestTwemoji)
#include "tst_twemoji.moc"
