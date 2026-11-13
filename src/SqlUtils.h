#pragma once

#include <QLatin1Char>
#include <QLatin1String>
#include <QPair>
#include <QString>

namespace SqlUtils {

inline QString quote(const QString &raw) {
    QString out = raw;
    out.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

inline QString escapeLike(const QString &raw) {
    QString out = raw;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('%'),  QLatin1String("\\%"));
    out.replace(QLatin1Char('_'),  QLatin1String("\\_"));
    return out;
}

inline QString containsPattern(const QString &raw) {
    return quote(QLatin1Char('%') + escapeLike(raw) + QLatin1Char('%'));
}

inline QString normalizeSearch(const QString &raw) {
    QString out = raw.toLower();
    out.replace(QChar(0x0451), QChar(0x0435));
    out.remove(QLatin1Char('\''));
    out.remove(QChar(0x2018));
    out.remove(QChar(0x2019));
    out.remove(QChar(0x02bc));
    return out;
}

inline QString normalizedSearchExpression(const QString &expression) {
    QString out = QStringLiteral("LOWER(%1)").arg(expression);
    for (ushort upper = 0x0410; upper <= 0x042f; ++upper) {
        out = QStringLiteral("REPLACE(%1, CHAR(%2), CHAR(%3))")
            .arg(out, QString::number(upper), QString::number(upper + 0x20));
    }
    out = QStringLiteral("REPLACE(%1, CHAR(1025), CHAR(1077))").arg(out);
    out = QStringLiteral("REPLACE(%1, CHAR(1105), CHAR(1077))").arg(out);
    return QStringLiteral(
        "REPLACE(REPLACE(REPLACE(REPLACE(%1, '''', ''), "
        "CHAR(8216), ''), CHAR(8217), ''), CHAR(700), '')")
        .arg(out);
}

inline QPair<QString, QString> pathPrefixBounds(const QString &dirPath) {
    QString lower = dirPath;
    if (!lower.endsWith(QLatin1Char('/'))) lower += QLatin1Char('/');
    QString upper = lower;
    upper[upper.size() - 1] = QChar(QLatin1Char('/').unicode() + 1);
    return {lower, upper};
}

}
