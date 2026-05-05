#include "TrackModel.h"

#include <QSqlError>
#include <QSqlQuery>

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
    QString sql = QStringLiteral("SELECT path FROM %1").arg(tableName());
    const QString f = filter();
    if (!f.isEmpty()) sql += QStringLiteral(" WHERE ") + f;
    const QString o = orderByClause();
    if (!o.isEmpty()) sql += QLatin1Char(' ') + o;
    sql += QStringLiteral(" LIMIT 1 OFFSET ") + QString::number(row);
    QSqlQuery q(database());
    q.setForwardOnly(true);
    if (!q.exec(sql) || !q.next()) return {};
    return q.value(0).toString();
}
