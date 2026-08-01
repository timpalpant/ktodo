/*
 * Tests for RichText::toHtml, which turns task text into the small HTML subset
 * Qt's rich text engine renders.
 *
 * Two properties matter here. Task text arrives from the server, so it is
 * attacker-influenced if anyone shares a project with you: escaping and scheme
 * filtering are a security boundary, not a nicety. And descriptions routinely
 * contain Markdown links and bare URLs that users expect to be clickable.
 */

#include "util/richtext.h"

#include <QTest>

class RichTextTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainTextIsEscaped_data();
    void plainTextIsEscaped();

    void markdownLinks_data();
    void markdownLinks();

    void bareUrls_data();
    void bareUrls();

    void dangerousSchemesAreNotLinked_data();
    void dangerousSchemesAreNotLinked();

    void newlinesBecomeBreaks();
    void ampersandInUrlIsEscapedOnce();
    void emptyInput();

    void hasLinks_data();
    void hasLinks();
};

void RichTextTest::plainTextIsEscaped_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "Buy milk" << "Buy milk";
    QTest::newRow("angle brackets") << "a < b > c" << "a &lt; b &gt; c";
    QTest::newRow("ampersand") << "Tom & Jerry" << "Tom &amp; Jerry";
    QTest::newRow("quotes") << R"(say "hi")" << "say &quot;hi&quot;";
    // A task title is not markup, so any tag in it must render literally.
    QTest::newRow("html tag") << "<b>bold</b>" << "&lt;b&gt;bold&lt;/b&gt;";
    QTest::newRow("script tag") << "<script>alert(1)</script>"
                                << "&lt;script&gt;alert(1)&lt;/script&gt;";
}

void RichTextTest::plainTextIsEscaped()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(RichText::toHtml(input), expected);
}

void RichTextTest::markdownLinks_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("simple") << "[QCAD](https://qcad.org)"
                            << R"HTML(<a href="https://qcad.org">QCAD</a>)HTML";

    QTest::newRow("label with spaces and dash") << "[QCAD - Download](https://qcad.org/en/download)"
                                                << R"HTML(<a href="https://qcad.org/en/download">QCAD - Download</a>)HTML";

    QTest::newRow("surrounded by text") << "see [docs](https://example.com/d) now"
                                        << R"(see <a href="https://example.com/d">docs</a> now)";

    // The target pattern allows one level of balanced parentheses so that
    // Wikipedia-style URLs are not truncated at the inner ')'.
    QTest::newRow("balanced parens in target") << "[Foo](https://e.com/Foo_(bar))"
                                               << R"HTML(<a href="https://e.com/Foo_(bar)">Foo</a>)HTML";

    QTest::newRow("label is escaped") << "[a<b](https://e.com)"
                                      << R"HTML(<a href="https://e.com">a&lt;b</a>)HTML";

    QTest::newRow("mailto") << "[mail](mailto:tim@example.com)"
                            << R"HTML(<a href="mailto:tim@example.com">mail</a>)HTML";
}

void RichTextTest::markdownLinks()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(RichText::toHtml(input), expected);
}

void RichTextTest::bareUrls_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("https") << "go to https://example.com"
                           << R"HTML(go to <a href="https://example.com">https://example.com</a>)HTML";

    // A bare www. host is what people paste; it needs a scheme to be a link.
    QTest::newRow("www gets a scheme") << "www.example.com"
                                       << R"HTML(<a href="https://www.example.com">www.example.com</a>)HTML";

    // Trailing sentence punctuation belongs to the sentence, not the address,
    // but must still appear in the output.
    QTest::newRow("trailing full stop") << "See https://example.com."
                                        << R"HTML(See <a href="https://example.com">https://example.com</a>.)HTML";

    QTest::newRow("trailing comma") << "https://example.com, then"
                                    << R"(<a href="https://example.com">https://example.com</a>, then)";

    QTest::newRow("unbalanced closing paren") << "(see https://example.com)"
                                              << R"((see <a href="https://example.com">https://example.com</a>))";

    QTest::newRow("path is kept") << "https://example.com/a/b?c=1"
                                  << R"HTML(<a href="https://example.com/a/b?c=1">https://example.com/a/b?c=1</a>)HTML";
}

void RichTextTest::bareUrls()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(RichText::toHtml(input), expected);
}

void RichTextTest::dangerousSchemesAreNotLinked_data()
{
    QTest::addColumn<QString>("input");

    // Only http, https and mailto may become anchors. Everything else has to
    // render as literal text: a shared task must not be able to plant a link
    // that runs script or opens a local file when clicked.
    QTest::newRow("javascript") << "[click](javascript:alert(1))";
    QTest::newRow("file") << "[open](file:///etc/passwd)";
    QTest::newRow("data") << "[img](data:text/html,<script>alert(1)</script>)";
    QTest::newRow("ftp") << "[get](ftp://example.com/x)";
}

void RichTextTest::dangerousSchemesAreNotLinked()
{
    QFETCH(QString, input);
    const QString html = RichText::toHtml(input);

    QVERIFY2(!html.contains(QLatin1String("<a ")), qPrintable(html));
    QCOMPARE(RichText::hasLinks(input), false);
}

void RichTextTest::newlinesBecomeBreaks()
{
    QCOMPARE(RichText::toHtml(QStringLiteral("a\nb")), QStringLiteral("a<br>b"));
}

void RichTextTest::ampersandInUrlIsEscapedOnce()
{
    // Links are located in the raw text and the pieces escaped separately, so
    // the query separator must not come out as "&amp;amp;".
    //
    // Raw string literals are kept out of the QVERIFY2 arguments: moc parses
    // macro arguments itself and miscounts the parentheses inside them.
    const QString html = RichText::toHtml(QStringLiteral("https://e.com/?a=1&b=2"));
    const QString doubleEscaped = QStringLiteral("&amp;amp;");
    const QString expectedHref = QStringLiteral("href=\"https://e.com/?a=1&amp;b=2\"");

    QVERIFY2(!html.contains(doubleEscaped), qPrintable(html));
    QVERIFY2(html.contains(expectedHref), qPrintable(html));
}

void RichTextTest::emptyInput()
{
    QCOMPARE(RichText::toHtml(QString()), QString());
    QCOMPARE(RichText::hasLinks(QString()), false);
}

void RichTextTest::hasLinks_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("expected");

    QTest::newRow("none") << "just text" << false;
    QTest::newRow("markdown") << "[a](https://e.com)" << true;
    QTest::newRow("bare") << "https://e.com" << true;
    QTest::newRow("www") << "www.e.com" << true;
    QTest::newRow("javascript") << "[a](javascript:x)" << false;
    // An email address on its own is not a link; only an explicit mailto: is.
    QTest::newRow("bare email") << "tim@example.com" << false;
}

void RichTextTest::hasLinks()
{
    QFETCH(QString, input);
    QFETCH(bool, expected);
    QCOMPARE(RichText::hasLinks(input), expected);
}

QTEST_APPLESS_MAIN(RichTextTest)

#include "richtexttest.moc"
