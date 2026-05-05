#pragma once

#include "TrackQueue.h"

#include <QAbstractListModel>

class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        PathRole,
        IsCurrentRole,
    };

    explicit QueueModel(TrackQueue *queue, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void remove(int position);

    void insertTrack(const Track &track);
    void appendTracks(const QList<Track> &tracks);
    void resetAll();
    void notifyCurrentChanged();

private:
    TrackQueue *m_queue;
};
