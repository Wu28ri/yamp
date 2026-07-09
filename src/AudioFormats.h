#pragma once

#include <QLatin1String>
#include <QStringList>

namespace AudioFormats {

inline const QStringList& nameFilters() {
    static const QStringList kFilters = {
        QStringLiteral("*.flac"), QStringLiteral("*.mp3"),
        QStringLiteral("*.wav"),  QStringLiteral("*.m4a"),
        QStringLiteral("*.mp4"),  QStringLiteral("*.aac"),
        QStringLiteral("*.ogg"),  QStringLiteral("*.oga"),
        QStringLiteral("*.opus"), QStringLiteral("*.wma"),
        QStringLiteral("*.aiff"), QStringLiteral("*.aif"),
        QStringLiteral("*.ape"),  QStringLiteral("*.alac"),
    };
    return kFilters;
}

}
