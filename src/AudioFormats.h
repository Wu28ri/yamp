#pragma once

#include <QLatin1Char>
#include <QSet>
#include <QString>
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

inline bool isAudioFile(const QString &filePath) {
    const int dot = filePath.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot + 1 >= filePath.size()) return false;
    const QString ext = filePath.mid(dot + 1).toLower();
    static const QSet<QString> kExtensions = {
        QStringLiteral("flac"), QStringLiteral("mp3"),
        QStringLiteral("wav"),  QStringLiteral("m4a"),
        QStringLiteral("mp4"),  QStringLiteral("aac"),
        QStringLiteral("ogg"),  QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("wma"),
        QStringLiteral("aiff"), QStringLiteral("aif"),
        QStringLiteral("ape"),  QStringLiteral("alac"),
    };
    return kExtensions.contains(ext);
}

}
