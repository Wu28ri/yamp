#pragma once

#include <QLatin1Char>
#include <QLatin1String>
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

inline QString prefixPattern(const QString &prefix) {
    return escapeLike(prefix) + QLatin1Char('%');
}

}
