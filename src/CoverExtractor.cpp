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
    TagLib::FileRef f(pathBytes.constData(), false);
    if (f.isNull() || !f.file()) return {};

    TagLib::ByteVector pic;

    if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(f.file())) {
        const auto &pictures = flac->pictureList();
        if (!pictures.isEmpty()) {
            const TagLib::FLAC::Picture *chosen = pictures.front();
            for (const auto *p : pictures) {
                if (p && p->type() == TagLib::FLAC::Picture::FrontCover && !p->data().isEmpty()) {
                    chosen = p;
                    break;
                }
            }
            if (chosen) pic = chosen->data();
        }
    } else if (auto *mp3 = dynamic_cast<TagLib::MPEG::File *>(f.file())) {
        if (auto *tag = mp3->ID3v2Tag()) {
            const auto &frames = tag->frameListMap()["APIC"];
            if (!frames.isEmpty()) {
                TagLib::ID3v2::AttachedPictureFrame *chosen = nullptr;
                for (auto *rawFrame : frames) {
                    auto *frame = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(rawFrame);
                    if (!frame || frame->picture().isEmpty()) continue;
                    if (!chosen) chosen = frame;
                    if (frame->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
                        chosen = frame;
                        break;
                    }
                }
                if (chosen) pic = chosen->picture();
            }
        }
    } else {
        const auto props = f.file()->complexProperties("PICTURE");
        if (!props.isEmpty()) {
            pic = props.front()["data"].toByteVector();
            for (const auto &prop : props) {
                const QString picType = QString::fromStdWString(prop["pictureType"].toString().toWString());
                const auto candidate = prop["data"].toByteVector();
                if (!candidate.isEmpty() && picType.compare(QStringLiteral("Front Cover"), Qt::CaseInsensitive) == 0) {
                    pic = candidate;
                    break;
                }
            }
        }
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

    auto scoreFileName = [](const QString &fileName) -> int {
        const QString base = QFileInfo(fileName).completeBaseName().toLower();
        if (base == QLatin1String("cover") || base == QLatin1String("folder")
            || base == QLatin1String("front") || base == QLatin1String("album")) {
            return 100;
        }
        if (base.contains(QLatin1String("back")) || base.contains(QLatin1String("inlay"))
            || base.contains(QLatin1String("booklet"))) {
            return 5;
        }
        if (base.contains(QLatin1String("cover")) || base.contains(QLatin1String("folder"))
            || base.contains(QLatin1String("front")) || base.contains(QLatin1String("album"))) {
            return 80;
        }
        return 20;
    };

    const QString dirPath = info.absolutePath();
    QString bestPath;
    int bestScore = -1;

    QDirIterator it(dirPath, nameFilters, QDir::Files);
    while (it.hasNext()) {
        const QString full = it.next();
        const QString fileName = QFileInfo(full).fileName();
        const int score = scoreFileName(fileName);
        if (score > bestScore || (score == bestScore && (bestPath.isEmpty() || full < bestPath))) {
            bestScore = score;
            bestPath = full;
            if (bestScore == 100 && QFileInfo(fileName).completeBaseName().compare(QStringLiteral("cover"), Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    return bestPath;
}

RawCoverData loadRawCover(const QString &trackPath) {
    RawCoverData result;

    QByteArray data = embeddedPicture(trackPath);
    if (!data.isEmpty()) {
        result.bytes  = std::move(data);
        result.source = Source::Embedded;
        return result;
    }

    const QString sidecar = sidecarImagePath(trackPath);
    if (!sidecar.isEmpty()) {
        QFile file(sidecar);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray raw = file.readAll();
            if (!raw.isEmpty()) {
                result.bytes       = std::move(raw);
                result.sidecarPath = sidecar;
                result.source      = Source::Sidecar;
                return result;
            }
        }
    }
    return result;
}

CoverData loadCoverWithBytes(const QString &trackPath) {
    CoverData result;
    RawCoverData raw = loadRawCover(trackPath);
    if (!raw.bytes.isEmpty()) {
        QImage image;
        if (image.loadFromData(raw.bytes)) {
            result.image  = std::move(image);
            result.bytes  = std::move(raw.bytes);
            result.source = raw.source;
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
