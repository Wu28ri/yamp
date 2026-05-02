#include "TrackModel.h"

TrackModel::TrackModel(QObject *parent) : QSqlTableModel(parent) {
    connect(this, &QAbstractItemModel::modelReset, this, [this]() {
        m_pathCacheValid = false;
        m_pathRowCache.clear();
    });
}

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

void TrackModel::ensurePathIndex() {
    if (m_pathCacheValid) return;
    while (canFetchMore()) fetchMore();
    const int rows = rowCount();
    m_pathRowCache.clear();
    m_pathRowCache.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        const QString path = QSqlTableModel::data(index(i, PathColumn), Qt::DisplayRole).toString();
        if (!path.isEmpty()) m_pathRowCache.insert(path, i);
    }
    m_pathCacheValid = true;
}

int TrackModel::rowForPath(const QString &path) {
    if (path.isEmpty()) return -1;
    ensurePathIndex();
    return m_pathRowCache.value(path, -1);
}
