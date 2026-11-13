#pragma once

#include "Track.h"

#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

namespace ScannerHelpers {

enum class TrackWriteResult { Inserted, Updated, Error };

class TrackInserter {
public:
    explicit TrackInserter(QSqlDatabase &db);

    TrackWriteResult upsert(const Track &track, qint64 fileSize, qint64 modifiedTime);

private:
    QSqlQuery m_upsertTrack;
    QSqlQuery m_findTrackId;
    QSqlQuery m_deleteTrackArtists;
    QSqlQuery m_upsertArtist;
    QSqlQuery m_findArtistId;
    QSqlQuery m_linkTrackArtist;
    QHash<QString, qint64> m_artistIdCache;
};

}
