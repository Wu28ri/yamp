#pragma once

#include <QSqlTableModel>

class TrackModel : public QSqlTableModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        PathRole,
        DurationRole,
        TrackNoRole,
        TechInfoRole,
    };

    enum Column {
        IdColumn       = 0,
        TitleColumn    = 1,
        ArtistColumn   = 2,
        AlbumColumn    = 3,
        PathColumn     = 4,
        DurationColumn = 5,
        SearchColumn   = 6,
        TrackNoColumn  = 7,
        TechInfoColumn = 8,
    };

    explicit TrackModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString pathForRow(int row);
    int rowForPath(const QString &path);

private:
    static int columnForRole(int role);
};
