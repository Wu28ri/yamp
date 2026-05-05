#include "AlbumModel.h"

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

void AlbumModel::refresh() {
    QString where;
    if (!m_search.isEmpty()) {
        const QString safe = sqlLikeEscape(m_search.toLower());
        where = QStringLiteral(
            "WHERE LOWER(album) LIKE '%%%1%%' ESCAPE '\\' "
            "OR LOWER(COALESCE(NULLIF(album_artist, ''), artist)) LIKE '%%%1%%' ESCAPE '\\' ").arg(safe);
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
    refresh();
}
