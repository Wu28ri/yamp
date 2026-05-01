#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace CoverExtractor {

QByteArray embeddedPicture(const QString &trackPath);
QString sidecarImagePath(const QString &trackPath);
QImage loadCover(const QString &trackPath);
QString detectImageExtension(const QByteArray &data);

}
