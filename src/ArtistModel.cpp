#include "ArtistModel.h"

#include "SqlUtils.h"

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

void ArtistModel::reload() {
    QString where;
    if (!m_search.isEmpty()) {
        const QString pattern =
            SqlUtils::containsPattern(SqlUtils::normalizeSearch(m_search));
        where = QStringLiteral(
            "WHERE REPLACE(REPLACE(REPLACE(REPLACE(REPLACE("
            "a.name_norm, CHAR(1105), CHAR(1077)), '''', ''), "
            "CHAR(8216), ''), CHAR(8217), ''), CHAR(700), '') LIKE %1 ESCAPE '\\' ")
            .arg(pattern);
    }
    setQuery(QStringLiteral(
        "SELECT a.name, "
        "COUNT(DISTINCT t.album COLLATE NOCASE) AS album_count, "
        "COUNT(DISTINCT t.id) AS track_count "
        "FROM artists a "
        "JOIN track_artists ta ON ta.artist_id = a.id "
        "JOIN tracks t ON t.id = ta.track_id ") + where + QStringLiteral(
        "GROUP BY a.id, a.name "
        "ORDER BY a.name COLLATE NOCASE"));
    while (canFetchMore()) fetchMore();
}

void ArtistModel::setSearch(const QString &query) {
    if (m_search == query) return;
    m_search = query;
    reload();
}
