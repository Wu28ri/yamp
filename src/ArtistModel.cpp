#include "ArtistModel.h"

ArtistModel::ArtistModel(QObject *parent) : QSqlQueryModel(parent) {}

QVariant ArtistModel::data(const QModelIndex &index, int role) const {
    if (role < Qt::UserRole) return QSqlQueryModel::data(index, role);
    int column = -1;
    switch (role) {
    case ArtistRole:     column = 0; break;
    case AlbumCountRole: column = 1; break;
    case TrackCountRole: column = 2; break;
    default:             return {};
    }
    return QSqlQueryModel::data(this->index(index.row(), column), Qt::DisplayRole);
}

QHash<int, QByteArray> ArtistModel::roleNames() const {
    return {
        {ArtistRole,     "artist"},
        {AlbumCountRole, "albumCount"},
        {TrackCountRole, "trackCount"},
    };
}

void ArtistModel::refresh() {
    setQuery(QStringLiteral(
        "SELECT a.name, "
        "COUNT(DISTINCT t.album) AS album_count, "
        "COUNT(DISTINCT t.id) AS track_count "
        "FROM artists a "
        "JOIN track_artists ta ON ta.artist_id = a.id "
        "JOIN tracks t ON t.id = ta.track_id "
        "GROUP BY a.id, a.name "
        "ORDER BY a.name COLLATE NOCASE"));
}
