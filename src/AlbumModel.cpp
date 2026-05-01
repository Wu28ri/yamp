#include "AlbumModel.h"

AlbumModel::AlbumModel(QObject *parent) : QSqlQueryModel(parent) {}

QVariant AlbumModel::data(const QModelIndex &index, int role) const {
    if (role < Qt::UserRole) return QSqlQueryModel::data(index, role);
    const int column = role - AlbumRole;
    if (column < 0 || column >= columnCount()) return {};
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
        "GROUP BY album "
        "ORDER BY album COLLATE NOCASE"));
}
