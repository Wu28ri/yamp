#pragma once

#include "Track.h"

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(QString rootPath, QObject *parent = nullptr);

    void run();

signals:
    void countDetermined(int totalFiles);
    void progress(int processed, int total);
    void batchReady();
    void finished(const QList<Track> &newTracks);

private:
    QString m_rootPath;
};

namespace MusicLibrary {

QString databasePath();
bool initialize();

QString normalizeArtistName(const QString &raw);
QStringList splitArtists(const QString &raw);
QString pickAlbumArtist(const QString &albumArtistTag, const QString &artist);
void linkTrackToArtists(QSqlDatabase &db, qint64 trackId, const QString &rawArtists);

void linkTrackToArtistsPrepared(qint64 trackId,
                                const QString &rawArtists,
                                QSqlQuery &upsertArtist,
                                QSqlQuery &findArtistId,
                                QSqlQuery &linkTrackArtist);

bool readTrackFromFile(const QString &filePath, Track &t, qint64 &fileSize);

}
