#pragma once

#include <QSqlQueryModel>

class ArtistModel : public QSqlQueryModel {
    Q_OBJECT
public:
    enum Roles {
        ArtistRole = Qt::UserRole + 1,
        AlbumCountRole,
        TrackCountRole,
    };

    explicit ArtistModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();
};
