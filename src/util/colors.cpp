#include "colors.h"

#include <QHash>

namespace Colors {

namespace {

const QHash<QString, QRgb> &palette()
{
    static const QHash<QString, QRgb> map = {
        {QStringLiteral("berry_red"), 0xb8256f}, {QStringLiteral("red"), 0xdb4035},         {QStringLiteral("orange"), 0xff9933},
        {QStringLiteral("yellow"), 0xfad000},    {QStringLiteral("olive_green"), 0xafb83b}, {QStringLiteral("lime_green"), 0x7ecc49},
        {QStringLiteral("green"), 0x299438},     {QStringLiteral("mint_green"), 0x6accbc},  {QStringLiteral("teal"), 0x158fad},
        {QStringLiteral("sky_blue"), 0x14aaf5},  {QStringLiteral("light_blue"), 0x96c3eb},  {QStringLiteral("blue"), 0x4073ff},
        {QStringLiteral("grape"), 0x884dff},     {QStringLiteral("violet"), 0xaf38eb},      {QStringLiteral("lavender"), 0xeb96eb},
        {QStringLiteral("magenta"), 0xe05194},   {QStringLiteral("salmon"), 0xff8d85},      {QStringLiteral("charcoal"), 0x808080},
        {QStringLiteral("grey"), 0xb8b8b8},      {QStringLiteral("taupe"), 0xccac93},
    };
    return map;
}

} // namespace

QColor fromTodoistName(const QString &name)
{
    const auto it = palette().constFind(name);
    if (it != palette().constEnd()) {
        return QColor::fromRgb(*it);
    }
    return QColor::fromRgb(palette().value(QStringLiteral("charcoal")));
}

QColor priorityColor(int apiPriority)
{
    switch (apiPriority) {
    case 4:
        return QColor::fromRgb(0xd1453b); // p1, urgent
    case 3:
        return QColor::fromRgb(0xeb8909); // p2
    case 2:
        return QColor::fromRgb(0x246fe0); // p3
    default:
        return QColor(); // p4 uses the theme's text color
    }
}

QStringList paletteNames()
{
    return {QStringLiteral("berry_red"),   QStringLiteral("red"),        QStringLiteral("orange"),     QStringLiteral("yellow"),
            QStringLiteral("olive_green"), QStringLiteral("lime_green"), QStringLiteral("green"),      QStringLiteral("mint_green"),
            QStringLiteral("teal"),        QStringLiteral("sky_blue"),   QStringLiteral("light_blue"), QStringLiteral("blue"),
            QStringLiteral("grape"),       QStringLiteral("violet"),     QStringLiteral("lavender"),   QStringLiteral("magenta"),
            QStringLiteral("salmon"),      QStringLiteral("charcoal"),   QStringLiteral("grey"),       QStringLiteral("taupe")};
}

} // namespace Colors
