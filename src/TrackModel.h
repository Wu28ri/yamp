#pragma once

#include <QHash>
#include <QSqlTableModel>
#include <QString>

class TrackModel : public QSqlTableModel {
    Q_OBJECT
public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        PathRole,
        DurationRole,
    };
    Q_ENUM(Role)

    explicit TrackModel(QObject *parent = nullptr);

    void setTable(const QString &tableName) override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int columnFor(const QString &columnName) const;
    Q_INVOKABLE QString pathForRow(int row);
    Q_INVOKABLE void ensureFetchedTo(int row);

private:
    void rebuildColumnCache();
    int columnFromRole(int role) const;

    QHash<QString, int> m_columnIndex;
};
