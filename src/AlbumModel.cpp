#include "AlbumModel.h"

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
    setQuery(QStringLiteral(
        "SELECT album, artist, MIN(path) AS path "
        "FROM tracks "
        "GROUP BY album, artist "
        "ORDER BY album COLLATE NOCASE, artist COLLATE NOCASE"));
}
