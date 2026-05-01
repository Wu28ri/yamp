#include "TrackModel.h"

TrackModel::TrackModel(QObject *parent) : QSqlTableModel(parent) {}

int TrackModel::columnForRole(int role) {
    switch (role) {
    case IdRole:       return IdColumn;
    case TitleRole:    return TitleColumn;
    case ArtistRole:   return ArtistColumn;
    case AlbumRole:    return AlbumColumn;
    case PathRole:     return PathColumn;
    case DurationRole: return DurationColumn;
    case TrackNoRole:  return TrackNoColumn;
    case TechInfoRole: return TechInfoColumn;
    default:           return -1;
    }
}

QVariant TrackModel::data(const QModelIndex &index, int role) const {
    if (role < Qt::UserRole) return QSqlTableModel::data(index, role);
    const int column = columnForRole(role);
    if (column < 0) return {};
    return QSqlTableModel::data(this->index(index.row(), column), Qt::DisplayRole);
}

QHash<int, QByteArray> TrackModel::roleNames() const {
    return {
        {IdRole,       "id"},
        {TitleRole,    "title"},
        {ArtistRole,   "artist"},
        {AlbumRole,    "album"},
        {PathRole,     "path"},
        {DurationRole, "duration"},
        {TrackNoRole,  "trackNo"},
        {TechInfoRole, "techInfo"},
    };
}

QString TrackModel::pathForRow(int row) {
    if (row < 0) return {};
    while (canFetchMore()) fetchMore();
    if (row >= rowCount()) return {};
    return QSqlTableModel::data(index(row, PathColumn), Qt::DisplayRole).toString();
}

int TrackModel::rowForPath(const QString &path) {
    if (path.isEmpty()) return -1;
    while (canFetchMore()) fetchMore();
    const int rows = rowCount();
    for (int i = 0; i < rows; ++i) {
        if (QSqlTableModel::data(index(i, PathColumn), Qt::DisplayRole).toString() == path) {
            return i;
        }
    }
    return -1;
}
