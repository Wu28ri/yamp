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
    // Total file count once enumeration finishes (phase 1).
    void countDetermined(int totalFiles);
    // Progress through phase 2 file processing.
    void progress(int processed, int total);
    // A batch of inserts has been committed; UI may now query DB and see them.
    void batchReady();
    // Scan finished, with the full list of newly inserted tracks.
    void finished(const QList<Track> &newTracks);

private:
    static QString makeTechInfo(const QString &filePath, int sampleRate, int bitrate, int bitDepth);

    QString m_rootPath;
};

namespace MusicLibrary {

QString databasePath();
bool initialize();

QString normalizeArtistName(const QString &raw);
QStringList splitArtists(const QString &raw);
void linkTrackToArtists(QSqlDatabase &db, qint64 trackId, const QString &rawArtists);

// Faster variant for hot loops: caller provides three prepared queries
// (insert-artist, lookup-artist-id, link-track-artist) so we avoid
// re-preparing on every track.
void linkTrackToArtistsPrepared(qint64 trackId,
                                const QString &rawArtists,
                                QSqlQuery &upsertArtist,
                                QSqlQuery &findArtistId,
                                QSqlQuery &linkTrackArtist);

}
