#pragma once

#include "Track.h"

#include <QSqlDatabase>
#include <QSqlQuery>

namespace ScannerHelpers {

class TrackInserter {
public:
    explicit TrackInserter(QSqlDatabase &db);

    bool insert(const Track &track, qint64 fileSize);
    bool hasError() const { return m_error; }

private:
    QSqlQuery m_insertTrack;
    QSqlQuery m_upsertArtist;
    QSqlQuery m_findArtistId;
    QSqlQuery m_linkTrackArtist;
    bool m_error = false;
};

}
