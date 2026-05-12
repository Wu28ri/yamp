#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace CoverExtractor {

enum class Source {
    None,
    Embedded,
    Sidecar,
};

struct CoverData {
    QImage     image;
    QByteArray bytes;
    Source     source = Source::None;
};

QByteArray embeddedPicture(const QString &trackPath);
QString sidecarImagePath(const QString &trackPath);
CoverData loadCoverWithBytes(const QString &trackPath);
QString detectImageExtension(const QByteArray &data);

}

