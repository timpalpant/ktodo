/*
 * Tests for the Todoist colour palette mapping.
 *
 * Colours are identified by name across the web, mobile and desktop clients,
 * so a project must keep the same colour everywhere. A typo in the table would
 * be invisible in review but obvious to a user, hence pinning the values.
 */

#include "util/colors.h"

#include <QTest>

class ColorsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void knownNames_data();
    void knownNames();

    void unknownNameFallsBackToCharcoal();
    void everyPaletteNameResolves();
    void paletteHasNoDuplicates();

    void priorityColors_data();
    void priorityColors();
};

void ColorsTest::knownNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QRgb>("rgb");

    QTest::newRow("berry_red") << "berry_red" << qRgb(0xb8, 0x25, 0x6f);
    QTest::newRow("red") << "red" << qRgb(0xdb, 0x40, 0x35);
    QTest::newRow("blue") << "blue" << qRgb(0x40, 0x73, 0xff);
    QTest::newRow("charcoal") << "charcoal" << qRgb(0x80, 0x80, 0x80);
    QTest::newRow("taupe") << "taupe" << qRgb(0xcc, 0xac, 0x93);
}

void ColorsTest::knownNames()
{
    QFETCH(QString, name);
    QFETCH(QRgb, rgb);
    QCOMPARE(Colors::fromTodoistName(name).rgb(), qRgb(qRed(rgb), qGreen(rgb), qBlue(rgb)));
}

void ColorsTest::unknownNameFallsBackToCharcoal()
{
    // The server may add palette entries at any time; an unknown one must show
    // a sensible colour rather than an invalid QColor that paints black.
    const QColor charcoal = Colors::fromTodoistName(QStringLiteral("charcoal"));

    QCOMPARE(Colors::fromTodoistName(QStringLiteral("chartreuse_surprise")), charcoal);
    QCOMPARE(Colors::fromTodoistName(QString()), charcoal);
    QVERIFY(Colors::fromTodoistName(QStringLiteral("nonsense")).isValid());
}

void ColorsTest::everyPaletteNameResolves()
{
    const QStringList names = Colors::paletteNames();
    QVERIFY(!names.isEmpty());

    const QColor charcoal = Colors::fromTodoistName(QStringLiteral("charcoal"));
    for (const QString &name : names) {
        const QColor c = Colors::fromTodoistName(name);
        QVERIFY2(c.isValid(), qPrintable(name));
        // Only "charcoal" itself may equal the fallback; anything else doing so
        // means the name is missing from the table.
        if (name != QLatin1String("charcoal")) {
            QVERIFY2(c != charcoal, qPrintable(QStringLiteral("%1 is missing from the palette").arg(name)));
        }
    }
}

void ColorsTest::paletteHasNoDuplicates()
{
    const QStringList names = Colors::paletteNames();
    QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());
}

void ColorsTest::priorityColors_data()
{
    QTest::addColumn<int>("apiPriority");
    QTest::addColumn<bool>("shouldBeValid");

    // API numbering: 4 is urgent (shown as P1), 1 is none (shown as P4).
    QTest::newRow("p1 urgent") << 4 << true;
    QTest::newRow("p2") << 3 << true;
    QTest::newRow("p3") << 2 << true;
    // P4 deliberately has no colour so the row uses the theme's text colour.
    QTest::newRow("p4 none") << 1 << false;
    QTest::newRow("out of range low") << 0 << false;
    QTest::newRow("out of range high") << 9 << false;
}

void ColorsTest::priorityColors()
{
    QFETCH(int, apiPriority);
    QFETCH(bool, shouldBeValid);
    QCOMPARE(Colors::priorityColor(apiPriority).isValid(), shouldBeValid);
}

QTEST_APPLESS_MAIN(ColorsTest)

#include "colorstest.moc"
