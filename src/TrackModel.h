#pragma once

#include <QSqlTableModel>

class TrackModel : public QSqlTableModel {
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        PathRole,
        DurationRole,
    };

    enum Column {
        TitleColumn    = 1,
        ArtistColumn   = 2,
        AlbumColumn    = 3,
        PathColumn     = 4,
        DurationColumn = 5,
        TrackNoColumn  = 7,
    };

    explicit TrackModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString pathForRow(int row);
    Q_INVOKABLE void ensureFetchedTo(int row);

private:
    static int columnForRole(int role);
};

