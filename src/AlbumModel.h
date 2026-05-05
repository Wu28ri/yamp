#pragma once

#include <QSqlQueryModel>
#include <QString>

class AlbumModel : public QSqlQueryModel {
    Q_OBJECT
public:
    enum Roles {
        AlbumRole = Qt::UserRole + 1,
        ArtistRole,
        PathRole,
    };

    explicit AlbumModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();
    void setSearch(const QString &query);

private:
    QString m_search;
};
