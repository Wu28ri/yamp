#include "ArtistModel.h"

namespace {
QString sqlLikeEscape(const QString &raw) {
    QString out = raw;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('%'),  QLatin1String("\\%"));
    out.replace(QLatin1Char('_'),  QLatin1String("\\_"));
    out.replace(QLatin1Char('\''), QLatin1String("''"));
    return out;
}
}

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
    QString where;
    if (!m_search.isEmpty()) {
        const QString safe = sqlLikeEscape(m_search.toLower());
        where = QStringLiteral("WHERE LOWER(a.name) LIKE '%%%1%%' ESCAPE '\\' ").arg(safe);
    }
    setQuery(QStringLiteral(
        "SELECT a.name, "
        "COUNT(DISTINCT t.album) AS album_count, "
        "COUNT(DISTINCT t.id) AS track_count "
        "FROM artists a "
        "JOIN track_artists ta ON ta.artist_id = a.id "
        "JOIN tracks t ON t.id = ta.track_id ") + where + QStringLiteral(
        "GROUP BY a.id, a.name "
        "ORDER BY a.name COLLATE NOCASE"));
}

void ArtistModel::setSearch(const QString &query) {
    if (m_search == query) return;
    m_search = query;
    refresh();
}
