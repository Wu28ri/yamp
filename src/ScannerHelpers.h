#pragma once

#include "Track.h"

#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

namespace ScannerHelpers {

class TrackInserter {
public:
    explicit TrackInserter(QSqlDatabase &db);

    bool insert(const Track &track, qint64 fileSize);

private:
    QSqlQuery m_insertTrack;
    QSqlQuery m_upsertArtist;
    QSqlQuery m_findArtistId;
    QSqlQuery m_linkTrackArtist;
};

struct InsertOutcome {
    int inserted = 0;
    QList<Track> tracks;
};

InsertOutcome insertTracksFromPaths(QSqlDatabase &db, const QStringList &paths);

int deleteTracksByPath(QSqlDatabase &db, const QStringList &paths);

bool updateTrackPath(QSqlDatabase &db, const QString &oldPath, const QString &newPath);

}
