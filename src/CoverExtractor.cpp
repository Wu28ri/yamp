#include "CoverExtractor.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tfile.h>

namespace CoverExtractor {

QByteArray embeddedPicture(const QString &trackPath) {
    if (trackPath.isEmpty()) return {};

    const QByteArray pathBytes = trackPath.toUtf8();
    TagLib::FileRef f(pathBytes.constData());
    if (f.isNull() || !f.file()) return {};

    TagLib::ByteVector pic;

    if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(f.file())) {
        const auto &pictures = flac->pictureList();
        if (!pictures.isEmpty()) pic = pictures.front()->data();
    } else if (auto *mp3 = dynamic_cast<TagLib::MPEG::File *>(f.file())) {
        if (auto *tag = mp3->ID3v2Tag()) {
            const auto &frames = tag->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                if (auto *frame = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front())) {
                    pic = frame->picture();
                }
            }
        }
    } else {
        const auto props = f.file()->complexProperties("PICTURE");
        if (!props.isEmpty()) pic = props.front()["DATA"].toByteVector();
    }

    if (pic.isEmpty()) return {};
    return QByteArray(pic.data(), static_cast<int>(pic.size()));
}

QString sidecarImagePath(const QString &trackPath) {
    if (trackPath.isEmpty()) return {};

    const QFileInfo info(trackPath);
    if (!info.exists()) return {};

    static const QStringList nameFilters = {
        QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
        QStringLiteral("*.png"), QStringLiteral("*.webp"),
        QStringLiteral("*.bmp")
    };
    static const QStringList preferredKeywords = {
        QStringLiteral("cover"), QStringLiteral("folder"),
        QStringLiteral("front"), QStringLiteral("album")
    };

    const QString dirPath = info.absolutePath();
    QString preferredBest;
    qint64 preferredBestSize = -1;
    QString fallbackBest;
    qint64 fallbackBestSize = -1;

    QDirIterator it(dirPath, nameFilters, QDir::Files);
    while (it.hasNext()) {
        const QString full = it.next();
        const QFileInfo fi(full);
        const QString fileName = fi.fileName();
        const qint64 sz = fi.size();
        bool isPreferred = false;
        for (const QString &keyword : preferredKeywords) {
            if (fileName.contains(keyword, Qt::CaseInsensitive)) { isPreferred = true; break; }
        }
        if (isPreferred) {
            if (sz > preferredBestSize) { preferredBestSize = sz; preferredBest = full; }
        } else {
            if (sz > fallbackBestSize)  { fallbackBestSize  = sz; fallbackBest  = full; }
        }
    }
    return preferredBest.isEmpty() ? fallbackBest : preferredBest;
}

QImage loadCover(const QString &trackPath) {
    return loadCoverWithBytes(trackPath).image;
}

CoverData loadCoverWithBytes(const QString &trackPath) {
    CoverData result;

    QByteArray data = embeddedPicture(trackPath);
    if (!data.isEmpty()) {
        QImage image;
        if (image.loadFromData(data)) {
            result.image = std::move(image);
            result.bytes = std::move(data);
            return result;
        }
    }

    const QString sidecar = sidecarImagePath(trackPath);
    if (!sidecar.isEmpty()) {
        QFile file(sidecar);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray raw = file.readAll();
            QImage image;
            if (!raw.isEmpty() && image.loadFromData(raw)) {
                result.image = std::move(image);
                result.bytes = std::move(raw);
                result.fromSidecar = true;
                return result;
            }
        }
        QImage image;
        if (image.load(sidecar)) {
            result.image = std::move(image);
            result.fromSidecar = true;
        }
    }
    return result;
}

QString detectImageExtension(const QByteArray &data) {
    if (data.size() >= 4) {
        const uchar *p = reinterpret_cast<const uchar *>(data.constData());
        if (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return QStringLiteral("jpg");
        if (p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47) return QStringLiteral("png");
        if (p[0] == 0x47 && p[1] == 0x49 && p[2] == 0x46 && p[3] == 0x38) return QStringLiteral("gif");
        if (p[0] == 0x42 && p[1] == 0x4D) return QStringLiteral("bmp");
        if (data.size() >= 12 && p[0] == 0x52 && p[1] == 0x49 && p[2] == 0x46 && p[3] == 0x46
            && p[8] == 0x57 && p[9] == 0x45 && p[10] == 0x42 && p[11] == 0x50)
            return QStringLiteral("webp");
    }
    return QStringLiteral("jpg");
}

}
