#include "TrackModel.h"

#include <QSqlQuery>
#include <QSqlRecord>

TrackModel::TrackModel(QObject *parent) : QSqlTableModel(parent) {}

void TrackModel::setTable(const QString &tableName) {
    QSqlTableModel::setTable(tableName);
    rebuildColumnCache();
}

void TrackModel::rebuildColumnCache() {
    m_columnIndex.clear();
    const QSqlRecord rec = record();
    for (int i = 0; i < rec.count(); ++i) {
        m_columnIndex.insert(rec.fieldName(i), i);
    }
}

int TrackModel::columnFor(const QString &columnName) const {
    return m_columnIndex.value(columnName, -1);
}

int TrackModel::columnFromRole(int role) const {
    switch (role) {
    case TitleRole:    return columnFor(QStringLiteral("title"));
    case ArtistRole:   return columnFor(QStringLiteral("artist"));
    case AlbumRole:    return columnFor(QStringLiteral("album"));
    case PathRole:     return columnFor(QStringLiteral("path"));
    case DurationRole: return columnFor(QStringLiteral("duration"));
    default:           return -1;
    }
}

QVariant TrackModel::data(const QModelIndex &index, int role) const {
    if (role < Qt::UserRole) return QSqlTableModel::data(index, role);
    const int column = columnFromRole(role);
    if (column < 0) return {};
    return QSqlTableModel::data(this->index(index.row(), column), Qt::DisplayRole);
}

QHash<int, QByteArray> TrackModel::roleNames() const {
    return {
        {TitleRole,    "title"},
        {ArtistRole,   "artist"},
        {AlbumRole,    "album"},
        {PathRole,     "path"},
        {DurationRole, "duration"},
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

void TrackModel::ensureFetchedTo(int row) {
    if (row < 0) return;
    while (rowCount() <= row && canFetchMore()) {
        fetchMore();
    }
}
