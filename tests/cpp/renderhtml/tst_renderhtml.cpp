// SPDX-License-Identifier: GPL-3.0-or-later
// TimelineModel::renderHtml: a message's HTML, as other people's servers
// and clients sent it, made into what Text.StyledText (Qt 5.6) is given.
// Reached through the model's "html" and "reply_to" roles, as the app
// reads them. Every tag the code knows is tested for what it becomes, and
// hostile input for what must not come through.
//
// Each test names the bug it exists to catch ("Planted-bug:"); it was
// trusted only after failing against that bug planted on purpose.

#include "timeline.h"
#include "Twemoji.h"

#include <QElapsedTimer>
#include <QImage>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString kDir = QStringLiteral(RUSTRX_ROOT "/qml/twemoji");
const int kEmote = 30;   // the model's emoteSize in these tests

QString U(const char *utf8) { return QString::fromUtf8(utf8); }

QString twemoji(const QString &name, int h)
{
    return QStringLiteral("<img src=\"file://%1/%2.png\" width=\"%3\" height=\"%3\" align=\"middle\">")
            .arg(kDir, name).arg(h);
}

QString spoiler() { return QStringLiteral("<i>") + qtTrId("rustrx-la-spoiler") + QStringLiteral("</i>"); }

// what may be in the output: the tags the renderer makes itself, nothing
// else. Every "<" must start one of them.
bool onlyOwnTags(const QString &out, QString *bad)
{
    static const QRegularExpression own(QStringLiteral(
        "^(?:</?(?:b|strong|i|u|p|pre|ul|ol|li|h[1-6])>"
        "|<br>"
        "|</a>|<a href=\"(?:https?|ftp|mailto|magnet|matrix):[^\"<>]*\">"
        "|</font>|<font color=\"#[0-9a-fA-F]{6}\">"
        "|<img src=\"file://[^\"<>]*\" width=\"\\d+\" height=\"\\d+\" align=\"middle\">)"),
        QRegularExpression::CaseInsensitiveOption);
    for (int i = out.indexOf(QLatin1Char('<')); i >= 0; i = out.indexOf(QLatin1Char('<'), i + 1)) {
        if (!own.match(out.mid(i, 300)).hasMatch()) {
            *bad = out.mid(i, 60);
            return false;
        }
    }
    return true;
}

}

class TestRenderHtml : public QObject
{
    Q_OBJECT

    Timeline t;
    QTemporaryDir tmp;
    QString emotePath;

private slots:
    void initTestCase()
    {
        Twemoji::init(kDir);
        t.model.setEmoteSize(kEmote);
        // a custom emoji twice as wide as high, its path with a quote in it
        QVERIFY(tmp.isValid());
        emotePath = tmp.path() + QStringLiteral("/a\"b.png");
        QImage pic(40, 20, QImage::Format_ARGB32);
        pic.fill(Qt::red);
        QVERIFY(pic.save(emotePath, "PNG"));
        t.model.setEmotePath(QStringLiteral("mxc://example.org/wide"), emotePath);
    }

    // Planted-bug: the reply's quote is drawn with Twemoji at the full
    // size (qRound(m_emoteSize * 0.75) becomes m_emoteSize).
    void plainAndEmpty()
    {
        QCOMPARE(t.html(QString()), QString());
        QCOMPARE(t.html(U("just text")), U("just text"));
        QCOMPARE(t.html(U("a 😀")), U("a ") + twemoji("1f600", kEmote));
        QCOMPARE(t.replyHtml(U("a 😀")), U("a ") + twemoji("1f600", qRound(kEmote * 0.75)));
    }

    // Planted-bug: a known tag keeps its attributes (open = tag instead of
    // the bare "<name>").
    void allowedTagsPassBare_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *n : { "b", "strong", "i", "u", "p", "pre", "ul", "ol", "li",
                               "h1", "h2", "h3", "h4", "h5", "h6" })
            QTest::newRow(n) << QString::fromLatin1(n);
    }
    void allowedTagsPassBare()
    {
        QFETCH(QString, name);
        const QString bare = QStringLiteral("<%1>x</%1>").arg(name);
        QCOMPARE(t.html(QStringLiteral("<%1 class=\"c\" onclick=\"evil()\" style=\"color:red\">x</%1>").arg(name)), bare);
        QCOMPARE(t.html(QStringLiteral("<%1>x</%1>").arg(name.toUpper())), bare);
    }

    // Planted-bug: a quote closes with "</p>" only, leaving the <i> open.
    void mappedTags_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("em") << U("<em>x</em>") << U("<i>x</i>");
        QTest::newRow("blockquote") << U("<blockquote>q</blockquote>a") << U("<p><i>q</i></p>a");
        QTest::newRow("br") << U("a<br>b<br/>c<BR />d") << U("a<br>b<br>c<br>d");
        QTest::newRow("hr") << U("a<hr>b") << U("a<br>―――――<br>b");
        QTest::newRow("table") << U("<table><tr><th>h</th></tr><tr><td>a</td><td>b</td></tr></table>")
                               << U("h | <br>a | b | <br>");
        QTest::newRow("div") << U("<div>a</div>b") << U("a<br>b");
        QTest::newRow("details") << U("<details><summary>S</summary>D</details>") << U("<b>S</b><br>D");
        QTest::newRow("caption") << U("<table><caption>C</caption></table>") << U("<b>C</b><br>");
    }
    void mappedTags()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: an unknown tag is passed through as it came (open =
    // tag), attributes and all.
    void droppedTagsKeepText_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        // the text of a script or style is shown, not run: StyledText runs
        // nothing, and hiding it would hide what was sent
        QTest::newRow("script") << U("<script>alert(1)</script>") << U("alert(1)");
        QTest::newRow("style") << U("<style>p{color:red}</style>x") << U("p{color:red}x");
        for (const char *n : { "code", "del", "s", "sup", "sub", "table", "iframe", "object", "svg",
                               "x-foo", "mx-reply", "marquee", "font", "span", "a" })
            QTest::newRow(n) << QStringLiteral("<%1 src=\"http://e/\" onload=\"x()\">t</%1>").arg(QLatin1String(n)) << U("t");
        QTest::newRow("comment") << U("a<!-- c -->b") << U("a&lt;!-- c --&gt;b");
    }
    void droppedTagsKeepText()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: the scheme check loses its "^" anchor, so any href with
    // "http:" somewhere in it is a link.
    void linkSchemes_data()
    {
        QTest::addColumn<QString>("href");
        QTest::addColumn<QString>("link");   // empty: no link, text only
        QTest::newRow("https") << U("https://e.org/x?a=1&amp;b=2") << U("https://e.org/x?a=1&amp;b=2");
        QTest::newRow("http") << U("http://e.org") << U("http://e.org");
        QTest::newRow("HTTPS") << U("HTTPS://E.ORG") << U("HTTPS://E.ORG");
        QTest::newRow("ftp") << U("ftp://e.org/f") << U("ftp://e.org/f");
        QTest::newRow("mailto") << U("mailto:a@e.org") << U("mailto:a@e.org");
        QTest::newRow("magnet") << U("magnet:?xt=urn:x") << U("magnet:?xt=urn:x");
        QTest::newRow("matrix") << U("matrix:r/a:e.org") << U("matrix:r/a:e.org");
        QTest::newRow("matrix.to") << U("https://matrix.to/#/@a:e.org") << U("https://matrix.to/#/@a:e.org");
        QTest::newRow("javascript") << U("javascript:alert(1)") << QString();
        QTest::newRow("JavaScript") << U("JaVaScRiPt:alert(1)") << QString();
        QTest::newRow("javascript with http in it") << U("javascript:x//http://e.org") << QString();
        QTest::newRow("entity in scheme") << U("jav&#x61;script:alert(1)") << QString();
        QTest::newRow("data") << U("data:text/html;base64,PHNjcmlwdD4=") << QString();
        QTest::newRow("file") << U("file:///etc/passwd") << QString();
        QTest::newRow("vbscript") << U("vbscript:x") << QString();
        QTest::newRow("tel") << U("tel:+491234") << QString();
        QTest::newRow("leading space") << U(" https://e.org") << QString();
        QTest::newRow("relative") << U("/x") << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void linkSchemes()
    {
        QFETCH(QString, href);
        QFETCH(QString, link);
        const QString out = t.html(QStringLiteral("<a href=\"%1\">t</a>").arg(href));
        QCOMPARE(out, link.isEmpty() ? U("t") : QStringLiteral("<a href=\"%1\">t</a>").arg(link));
    }

    // Planted-bug: the quote in an href is not made %22, so it ends the
    // attribute early in what StyledText reads.
    void linkAttributes_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("single quotes") << U("<a href='http://e.org'>t</a>") << U("<a href=\"http://e.org\">t</a>");
        QTest::newRow("quote inside") << U("<a href='http://e.org/\"onclick=\"x'>t</a>")
                                      << U("<a href=\"http://e.org/%22onclick=%22x\">t</a>");
        QTest::newRow("unquoted") << U("<a href=http://e.org>t</a>") << U("t");
        QTest::newRow("other attributes") << U("<a title=\"x\" target=\"_blank\" href=\"http://e.org\" onclick=\"y\">t</a>")
                                          << U("<a href=\"http://e.org\">t</a>");
        // "\bhref" also matches data-href, the first one wins: still only
        // an allowed scheme gets through
        QTest::newRow("data-href first, bad") << U("<a data-href=\"javascript:x\" href=\"http://e.org\">t</a>") << U("t");
        QTest::newRow("data-href first, good") << U("<a data-href=\"http://e.org\" href=\"javascript:x\">t</a>")
                                               << U("<a href=\"http://e.org\">t</a>");
        QTest::newRow("gt in href") << U("<a href=\"http://e.org/>x\">t</a>") << U("x\"&gt;t");
        QTest::newRow("nested a") << U("<a href=\"http://a\">1<a href=\"http://b\">2</a>3</a>")
                                  << U("<a href=\"http://a\">1<a href=\"http://b\">2</a>3</a>");
    }
    void linkAttributes()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: an <img>'s alt text is put out with its "<" as it is.
    void imagesBecomeText_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("remote picture") << U("<img src=\"https://e.org/track.png\" alt=\"pic\">") << U("pic");
        QTest::newRow("title instead") << U("<img src=\"https://e.org/p.png\" title=\"T\">") << U("T");
        QTest::newRow("onerror") << U("<img src=x onerror=alert(1)>") << QString();
        QTest::newRow("lt in alt") << U("<img alt='a<b' src=\"x\">") << U("a&lt;b");
        QTest::newRow("uncached mxc") << U("<img src=\"mxc://e.org/none\" alt=\":blob:\">") << U(":blob:");
        QTest::newRow("mxc without alt") << U("<img src=\"mxc://e.org/none\">") << QString();
        // the alt goes out as it is: no Twemoji for an emoji in it
        QTest::newRow("emoji alt") << U("<img alt=\"😀\" src=\"x\">") << U("😀");
        QTest::newRow("entity alt") << U("<img alt=\"&lt;b&gt;\" src=\"x\">") << U("&lt;b&gt;");
    }
    void imagesBecomeText()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: the custom emoji's width is computed from the wrong
    // side (height over width), so a wide one comes out narrow.
    void customEmoji()
    {
        const QString pic = QStringLiteral("<img src=\"file://%1\" width=\"60\" height=\"30\" align=\"middle\">")
                .arg(emotePath.toHtmlEscaped());
        QVERIFY(pic.contains(QStringLiteral("a&quot;b.png")));
        QCOMPARE(t.html(U("a<img src=\"mxc://example.org/wide\" alt=\":w:\">b")), U("a") + pic + U("b"));
        // in a reply's quote the Twemoji are smaller, a custom emoji is not
        QCOMPARE(t.replyHtml(U("<img src=\"mxc://example.org/wide\" alt=\":w:\">😀")),
                 pic + twemoji("1f600", qRound(kEmote * 0.75)));
    }

    // Planted-bug: closing a spoiler does not count it out (no --hidden),
    // so everything after the first spoiler is hidden.
    void spoilers_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("span") << U("a<span data-mx-spoiler>secret</span>b") << U("a") + spoiler() + U("b");
        QTest::newRow("with reason") << U("<span data-mx-spoiler=\"why\">s</span>b") << spoiler() + U("b");
        QTest::newRow("font") << U("<font data-mx-spoiler>s</font>b") << spoiler() + U("b");
        QTest::newRow("case") << U("<SPAN DATA-MX-SPOILER>s</SPAN>b") << spoiler() + U("b");
        QTest::newRow("nested") << U("<span data-mx-spoiler>x<span data-mx-spoiler>y</span>z</span>w") << spoiler() + U("w");
        QTest::newRow("two") << U("<span data-mx-spoiler>x</span>-<span data-mx-spoiler>y</span>.")
                             << spoiler() + U("-") + spoiler() + U(".");
        QTest::newRow("link, picture, emoji inside")
                << U("<span data-mx-spoiler><a href=\"http://e\">l</a><img src=\"mxc://example.org/wide\">😀<br></span>")
                << spoiler();
        QTest::newRow("closed by its parent") << U("<b><span data-mx-spoiler>s</b>after") << U("<b>") + spoiler() + U("</b>after");
        QTest::newRow("unclosed hides the rest") << U("<span data-mx-spoiler>s<b>t</b>") << spoiler();
        QTest::newRow("only span and font") << U("<div data-mx-spoiler>s</div>") << U("s<br>");
    }
    void spoilers()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: the reply expression loses DotMatchesEverything, so a
    // quoted fallback with a line break in it stays.
    void replyFallback_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("stripped") << U("<mx-reply><blockquote><a href=\"https://matrix.to/#/!r\">In reply to</a> q</blockquote></mx-reply>answer")
                                  << U("answer");
        QTest::newRow("line breaks") << U("<mx-reply>a\nb\n</mx-reply>c") << U("c");
        QTest::newRow("case") << U("<MX-REPLY>q</Mx-Reply>c") << U("c");
        // greedy: from the first open to the last close
        QTest::newRow("two, all between goes") << U("<mx-reply>q1</mx-reply>mid<mx-reply>q2</mx-reply>end") << U("end");
        // without its close it is an unknown tag: its text shows
        QTest::newRow("unclosed") << U("<mx-reply>q</b>c") << U("qc");
    }
    void replyFallback()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: the colour check loses its "$", so a longer value
    // ("#ff00001") gets through.
    void colours_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        const QString red = U("<font color=\"#ff0000\">x</font>");
        QTest::newRow("span data-mx-color") << U("<span data-mx-color=\"#ff0000\">x</span>") << red;
        QTest::newRow("font data-mx-color") << U("<font data-mx-color=\"#ff0000\">x</font>") << red;
        QTest::newRow("font color") << U("<font color=\"#ff0000\">x</font>") << red;
        QTest::newRow("upper case kept") << U("<font color=\"#FF00aa\">x</font>") << U("<font color=\"#FF00aa\">x</font>");
        QTest::newRow("data-mx wins") << U("<font data-mx-color=\"#ff0000\" color=\"#00ff00\">x</font>") << red;
        QTest::newRow("span color ignored") << U("<span color=\"#ff0000\">x</span>") << U("x");
        QTest::newRow("name") << U("<font color=\"red\">x</font>") << U("x");
        QTest::newRow("short") << U("<font color=\"#f00\">x</font>") << U("x");
        QTest::newRow("long") << U("<font color=\"#ff00001\">x</font>") << U("x");
        QTest::newRow("injection") << U("<font color='#ff0000\" size=\"99'>x</font>") << U("x");
        QTest::newRow("background only") << U("<span data-mx-bg-color=\"#ff0000\">x</span>") << U("x");
        QTest::newRow("unclosed") << U("<font color=\"#ff0000\">x") << red;
    }
    void colours()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: a close tag only closes the innermost open tag, not
    // everything down to its own open.
    void nesting_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("misnested") << U("<b><i>x</b>y</i>") << U("<b><i>x</i></b>y");
        QTest::newRow("close without open") << U("</b>x</p>") << U("x");
        QTest::newRow("unclosed, closed at the end") << U("<b><i><u>x") << U("<b><i><u>x</u></i></b>");
        QTest::newRow("list items unclosed") << U("<ul><li>a<li>b</ul>") << U("<ul><li>a<li>b</li></li></ul>");
        QTest::newRow("skips a dropped tag") << U("<b><code>x</b>y") << U("<b>x</b>y");
    }
    void nesting()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: "&#60;" decodes to a bare "<" instead of "&lt;".
    void entities_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("lt gt decimal") << U("&#60;b&#62;") << U("&lt;b&gt;");
        QTest::newRow("lt hex") << U("&#x3C;img src=x&#x3e;") << U("&lt;img src=x&gt;");
        QTest::newRow("amp") << U("&#38;lt;") << U("&amp;lt;");
        QTest::newRow("apos") << U("it&apos;s &#39;") << U("it's '");
        QTest::newRow("letter") << U("&#65;&#x42;") << U("AB");
        QTest::newRow("emoji, then Twemoji") << U("&#128512;") << twemoji("1f600", kEmote);
        QTest::newRow("zero dropped") << U("a&#0;b") << U("ab");
        QTest::newRow("too large dropped") << U("a&#x110000;b") << U("ab");
        QTest::newRow("named pass") << U("&lt;&gt;&amp;&quot;&nbsp;") << U("&lt;&gt;&amp;&quot;&nbsp;");
        QTest::newRow("in attribute not decoded") << U("<a href=\"&#106;avascript:x\">t</a>") << U("t");
    }
    void entities()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(t.html(in), out);
    }

    // Planted-bug: an <img>'s alt text is put out with its "<" as it is
    // (the same plant as imagesBecomeText, caught here by the tag check).
    // The last four rows failed before the fix of audit finding I2.
    void outputHasOnlyOwnTags_data()
    {
        QTest::addColumn<QString>("in");
        QTest::newRow("script") << U("<script>alert(1)</script>");
        QTest::newRow("img onerror") << U("<img src=x onerror=alert(1)>");
        QTest::newRow("alt with lt") << U("<img alt='<b' src=x>");
        QTest::newRow("svg") << U("<svg><a xlink:href=\"javascript:x\">t</a></svg>");
        QTest::newRow("iframe") << U("<iframe src=\"https://e.org\"></iframe>");
        QTest::newRow("entities") << U("&#60;img src=&#34;http://e.org/p.png&#34;&#62;");
        QTest::newRow("unclosed tag") << U("<b");
        // a "<" that starts no tag the expression knows is text, escaped;
        // before the fix it went out raw, the next tag's "<x>" was dropped
        // and what followed the stray "<" read as a tag of its own
        QTest::newRow("stray lt, picture") << U("<<x>img src=\"http://e.org/p.png\">");
        QTest::newRow("stray lt, link") << U("<<x>a href=\"javascript:alert(1)\">t</a>");
        QTest::newRow("lt and digit") << U("1 <2 and 3> 0");
    }
    void outputHasOnlyOwnTags()
    {
        QFETCH(QString, in);
        QString bad;
        const bool ok = onlyOwnTags(t.html(in), &bad);
        QVERIFY2(ok, qPrintable(bad));
    }

    // Audit finding I2 (2026-11-17), test first: the text between tags is
    // not escaped, and StyledText skips white space after "<", so "< img"
    // is a picture it fetches and "< a" a link with any scheme. The
    // hostile rows fail until the fix and are marked so; the last row is
    // the guard that must keep passing.
    // Planted-bug (guard row): "&#60;" decodes to a bare "<".
    void auditI2SpaceAfterLessThan_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");   // empty: only "no tag StyledText would parse"
        QTest::newRow("I2 img") << U("< img src=\"https://x/p.png\">") << QString();
        QTest::newRow("I2 img, two spaces") << U("<  img src=\"https://x/p.png\">after") << QString();
        QTest::newRow("I2 img, tab") << U("<\timg src=\"https://x/p.png\">") << QString();
        QTest::newRow("I2 link") << U("< a href=\"javascript:alert(1)\">t</a>") << QString();
        QTest::newRow("guard: entity stays text") << U("&#60;img src=x&#62;") << U("&lt;img src=x&gt;");
    }
    void auditI2SpaceAfterLessThan()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        const QString got = t.html(in);
        if (!out.isEmpty())
            QCOMPARE(got, out);
        static const QRegularExpression parsed(QStringLiteral("<\\s*(img|a)\\b"), QRegularExpression::CaseInsensitiveOption);
        QVERIFY2(!parsed.match(got).hasMatch(), qPrintable(got));
    }

    // Audit finding I3 (2026-11-17), test first: a "'" in an href is
    // passed on, and StyledText ends the value there and takes the second
    // href it then finds. Every <a> must be one href, quoted with '"',
    // holding no quote. The I3 row fails until the fix; the guard rows
    // must keep passing.
    // Planted-bug (guard rows): the '"' in an href is not made %22.
    void auditI3QuoteInHtmlHref_data()
    {
        QTest::addColumn<QString>("in");
        QTest::newRow("I3 single quote") << U("<a href=\"https://a/'href='file:///etc/passwd'\">x</a>");
        QTest::newRow("guard: double quote in single-quoted href") << U("<a href='https://a/\"href=\"file:///p'>x</a>");
        QTest::newRow("guard: plain link") << U("<a href=\"https://a/b?c=1&amp;d=2\">x</a>");
    }
    void auditI3QuoteInHtmlHref()
    {
        QFETCH(QString, in);
        const QString got = t.html(in);
        QVERIFY(got.contains(QLatin1String("<a ")));
        static const QRegularExpression one(QStringLiteral("^<a href=\"https://[^\"'\\s<>]*\">"));
        QVERIFY2(one.match(got.mid(got.indexOf(QLatin1String("<a ")))).hasMatch(), qPrintable(got));
    }

    // Planted-bug: the tags still open at the end are not closed (the last
    // loop of renderHtml dropped).
    void hugeInput()
    {
        QElapsedTimer timer;
        timer.start();
        // 1 MB of text; a Matrix event is at most 64 KiB, so this is far
        // past anything a server delivers
        const QString words = QStringLiteral("word ").repeated(200000);
        QCOMPARE(t.html(words), words);
        const int n = 100000;
        const QString nested = t.html(QStringLiteral("<b>").repeated(n) + QStringLiteral("x"));
        QCOMPARE(nested, QStringLiteral("<b>").repeated(n) + QStringLiteral("x") + QStringLiteral("</b>").repeated(n));
        QCOMPARE(t.html(QStringLiteral("<span data-mx-spoiler>").repeated(n) + QStringLiteral("x")), spoiler());
        QCOMPARE(t.html(QStringLiteral("</i>").repeated(n) + QStringLiteral("x")), QStringLiteral("x"));
        QVERIFY2(timer.elapsed() < 30000, qPrintable(QString::number(timer.elapsed())));
    }

    // Planted-bug: the same as hugeInput (the last closing loop dropped):
    // the 64 KiB worst cases also check their closing tags.
    void worstCaseEventSize()
    {
        // inputs that make a regular expression scan to the end from every
        // position; at the 64 KiB a Matrix event can have, each must stay
        // well under a second on the host (the phone is some 5 to 10 times
        // slower)
        const int size = 65536;
        struct Case { const char *name; QString in; };
        const Case cases[] = {
            { "open tags without >", QStringLiteral("<a").repeated(size / 2) },
            { "reply opens without close", QStringLiteral("<mx-reply>").repeated(size / 10) },
            { "entities without ;", QStringLiteral("&#1").repeated(size / 3) },
            { "lt only", QStringLiteral("<").repeated(size) },
        };
        for (const Case &c : cases) {
            QElapsedTimer timer;
            timer.start();
            t.html(c.in);
            qInfo("%s: %lld ms", c.name, static_cast<long long>(timer.elapsed()));
            QVERIFY2(timer.elapsed() < 1000, c.name);
        }
        const QString closed = t.html(QStringLiteral("<i>").repeated(size / 3));
        QCOMPARE(closed.count(QStringLiteral("</i>")), size / 3);
    }
};

QTEST_GUILESS_MAIN(TestRenderHtml)
#include "tst_renderhtml.moc"
