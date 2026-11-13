#include "CoverCacheService.h"

#include "CoverExtractor.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

namespace {
constexpr int kPruneKeepCount = 256;
}

CoverCacheService::CoverCacheService(QObject *parent) : QObject(parent) {
    m_pool.setMaxThreadCount(2);
}

CoverCacheService::~CoverCacheService() {
    m_pool.clear();
    m_pool.waitForDone();
}

QString CoverCacheService::cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = base + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

QString CoverCacheService::pathForHash(const QByteArray &hash, const QString &ext) {
    return cacheDir() + QLatin1Char('/')
           + QString::fromLatin1(hash.toHex()) + QLatin1Char('.') + ext;
}

bool CoverCacheService::writeAtomic(const QString &targetPath, const QByteArray &data) {
    if (QFileInfo::exists(targetPath)) {
        QFile existing(targetPath);
        if (existing.open(QIODevice::ReadWrite)) {
            existing.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
        }
        return true;
    }
    const QString tmpPath = targetPath + QStringLiteral(".tmp.")
                            + QString::number(QCoreApplication::applicationPid())
                            + QLatin1Char('.')
                            + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        if (tmp.write(data) != data.size()) {
            tmp.close();
            QFile::remove(tmpPath);
            return false;
        }
        tmp.flush();
        tmp.close();
    }
    if (QFileInfo::exists(targetPath)) {
        QFile::remove(tmpPath);
        return true;
    }
    if (!QFile::rename(tmpPath, targetPath)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

void CoverCacheService::pruneTo(int keepCount) {
    const QString dir = cacheDir();
    QDir d(dir);
    const auto allEntries = d.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Time | QDir::Reversed);

    const QDateTime staleTmpCutoff = QDateTime::currentDateTimeUtc().addSecs(-300);
    QFileInfoList cacheFiles;
    cacheFiles.reserve(allEntries.size());
    for (const QFileInfo &info : allEntries) {
        if (info.fileName().contains(QStringLiteral(".tmp."))) {
            if (info.lastModified().toUTC() < staleTmpCutoff) {
                QFile::remove(info.absoluteFilePath());
            }
            continue;
        }
        cacheFiles.push_back(info);
    }

    if (cacheFiles.size() <= keepCount) return;
    for (int i = 0; i < cacheFiles.size() - keepCount; ++i) {
        QFile::remove(cacheFiles[i].absoluteFilePath());
    }
}

void CoverCacheService::requestCoverFor(const QString &trackPath, quint64 generation) {
    if (trackPath.isEmpty()) return;
    m_latestGeneration.store(generation, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.start([this, trackPath, generation]() {
        if (m_latestGeneration.load(std::memory_order_relaxed) != generation) return;
        const QByteArray data = CoverExtractor::embeddedPicture(trackPath);
        if (m_latestGeneration.load(std::memory_order_relaxed) != generation) return;
        QString resolved;
        if (!data.isEmpty()) {
            const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
            const QString ext = CoverExtractor::detectImageExtension(data);
            const QString out = pathForHash(hash, ext);
            if (writeAtomic(out, data)) {
                resolved = out;
                pruneTo(kPruneKeepCount);
            }
        } else {
            resolved = CoverExtractor::sidecarImagePath(trackPath);
        }

        if (m_latestGeneration.load(std::memory_order_relaxed) != generation) return;
        QMetaObject::invokeMethod(this, [this, trackPath, resolved, generation]() {
            emit coverResolved(trackPath, resolved, generation);
        }, Qt::QueuedConnection);
    });
}
