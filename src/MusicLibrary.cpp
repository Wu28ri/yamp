#include "MusicLibrary.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include <taglib/fileref.h>
#include <taglib/tag.h>

namespace MusicLibrary {

QString databasePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/music_library.db");
}

bool initialize() {
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid()) {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
        db.setDatabaseName(databasePath());
    }
    if (!db.isOpen() && !db.open()) {
        qWarning() << "[MusicLibrary] failed to open DB:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    const bool ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, artist TEXT, album TEXT, "
        "path TEXT UNIQUE, duration INTEGER, "
        "search_text TEXT, track_no INTEGER, "
        "tech_info TEXT)"));
    if (!ok) qWarning() << "[MusicLibrary] schema:" << q.lastError().text();
    return ok;
}

}

LibraryScanner::LibraryScanner(QString rootPath, QObject *parent)
    : QObject(parent), m_rootPath(std::move(rootPath)) {}

QString LibraryScanner::makeTechInfo(const QString &filePath, int sampleRate, int bitrate) {
    const double khz = sampleRate / 1000.0;
    QString prefix;
    if (filePath.endsWith(QLatin1String(".flac"), Qt::CaseInsensitive)) prefix = QStringLiteral("FLAC | ");
    else if (filePath.endsWith(QLatin1String(".mp3"), Qt::CaseInsensitive)) prefix = QStringLiteral("MP3 | ");
    return QStringLiteral("%1%2 kHz | %3 kbps")
        .arg(prefix)
        .arg(khz, 0, 'f', 1)
        .arg(bitrate);
}

void LibraryScanner::run() {
    if (m_rootPath.isEmpty()) {
        emit finished({});
        return;
    }

    const QString connName = QStringLiteral("yamp_scan_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QList<Track> newTracks;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(MusicLibrary::databasePath());
        if (!db.open()) {
            qWarning() << "[Scan] failed to open DB:" << db.lastError().text();
            QSqlDatabase::removeDatabase(connName);
            emit finished({});
            return;
        }

        if (!db.transaction()) {
            qWarning() << "[Scan] BEGIN failed:" << db.lastError().text();
        }

        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO tracks "
            "(title, artist, album, path, duration, search_text, track_no, tech_info) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

        QDirIterator it(m_rootPath,
                        {QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
                        QDir::Files,
                        QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString filePath = it.next();
            const QByteArray pathBytes = filePath.toUtf8();
            TagLib::FileRef f(pathBytes.constData());

            QString title = it.fileName();
            QString artist = QStringLiteral("Unknown Artist");
            QString album = QStringLiteral("Unknown Album");
            QString techInfo;
            int duration = 0;
            int trackNo = 0;

            if (!f.isNull()) {
                if (auto *tag = f.tag()) {
                    const QString tTitle = QString::fromStdString(tag->title().to8Bit(true));
                    const QString tArtist = QString::fromStdString(tag->artist().to8Bit(true));
                    const QString tAlbum = QString::fromStdString(tag->album().to8Bit(true));
                    if (!tTitle.isEmpty())  title  = tTitle;
                    if (!tArtist.isEmpty()) artist = tArtist;
                    if (!tAlbum.isEmpty())  album  = tAlbum;
                    trackNo = tag->track();
                }
                if (auto *audio = f.audioProperties()) {
                    duration = audio->lengthInSeconds();
                    techInfo = makeTechInfo(filePath, audio->sampleRate(), audio->bitrate());
                }
            }

            const QString searchText = (title + QLatin1Char(' ') + artist + QLatin1Char(' ') + album).toLower();

            q.bindValue(0, title);
            q.bindValue(1, artist);
            q.bindValue(2, album);
            q.bindValue(3, filePath);
            q.bindValue(4, duration);
            q.bindValue(5, searchText);
            q.bindValue(6, trackNo);
            q.bindValue(7, techInfo);

            if (q.exec() && q.numRowsAffected() > 0) {
                newTracks.append({filePath, title, artist, album, duration, techInfo, trackNo});
            }
        }

        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    emit finished(newTracks);
}
