#include "AlbumModel.h"

#include "SqlUtils.h"

AlbumModel::AlbumModel(QObject *parent) : QSqlQueryModel(parent) {}

QVariant AlbumModel::data(const QModelIndex &index, int role) const {
    if (role < Qt::UserRole) return QSqlQueryModel::data(index, role);
    int column = -1;
    switch (role) {
    case AlbumRole:  column = 0; break;
    case ArtistRole: column = 1; break;
    case PathRole:   column = 2; break;
    default:         return {};
    }
    return QSqlQueryModel::data(this->index(index.row(), column), Qt::DisplayRole);
}

QHash<int, QByteArray> AlbumModel::roleNames() const {
    return {
        {AlbumRole,  "album"},
        {ArtistRole, "artist"},
        {PathRole,   "path"},
    };
}

void AlbumModel::reload() {
    QString where;
    if (!m_search.isEmpty()) {
        const QString pattern =
            SqlUtils::containsPattern(SqlUtils::normalizeSearch(m_search));
        where = QStringLiteral(
            "WHERE %1 LIKE %3 ESCAPE '\\' "
            "OR %2 LIKE %3 ESCAPE '\\' ")
            .arg(SqlUtils::normalizedSearchExpression(QStringLiteral("album")),
                 SqlUtils::normalizedSearchExpression(QStringLiteral(
                     "COALESCE(NULLIF(album_artist, ''), artist)")),
                 pattern);
    }
    setQuery(QStringLiteral(
        "SELECT album, "
        "COALESCE(NULLIF(album_artist, ''), artist) AS artist, "
        "MIN(path) AS path "
        "FROM tracks ") + where + QStringLiteral(
        "GROUP BY album COLLATE NOCASE, "
        "COALESCE(NULLIF(album_artist, ''), artist) COLLATE NOCASE "
        "ORDER BY album COLLATE NOCASE, artist COLLATE NOCASE"));
}

void AlbumModel::setSearch(const QString &query) {
    if (m_search == query) return;
    m_search = query;
    reload();
}
