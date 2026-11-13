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
        TrackNoRole,
        DiscNoRole,
    };
    Q_ENUM(Role)

    explicit TrackModel(QObject *parent = nullptr);

    void setTable(const QString &tableName) override;
    void setSortSpec(const QString &columnName, Qt::SortOrder order);
    static QString orderSqlClause(const QString &columnName, Qt::SortOrder order);
    static QString orderColumnsClause(const QString &columnName, Qt::SortOrder order);

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int columnFor(const QString &columnName) const;
    Q_INVOKABLE QString pathForRow(int row);
    Q_INVOKABLE void ensureFetchedTo(int row);

protected:
    QString orderByClause() const override;

private:
    void rebuildColumnCache();
    int columnFromRole(int role) const;

    QHash<QString, int> m_columnIndex;
    QString m_sortColumnName = QStringLiteral("title");
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
