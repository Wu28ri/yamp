#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace CoverExtractor {

struct CoverData {
    QImage     image;
    QByteArray bytes;      // raw encoded picture bytes (for hashing/deduping)
    bool       fromSidecar = false;
};

QByteArray embeddedPicture(const QString &trackPath);
QString sidecarImagePath(const QString &trackPath);
QImage loadCover(const QString &trackPath);
CoverData loadCoverWithBytes(const QString &trackPath);
QString detectImageExtension(const QByteArray &data);

}
