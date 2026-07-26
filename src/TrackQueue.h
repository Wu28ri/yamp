#pragma once

#include "Track.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <vector>

class TrackQueue {
public:
    int  currentPosition() const { return m_currentDetached ? -1 : m_currentIndex; }
    int  nextInsertionPosition() const;
    int  count()           const { return static_cast<int>(m_playOrder.size()); }
    bool isShuffle()       const { return m_shuffle; }

    bool containsPath(const QString &path) const;
    int  positionOfPath(const QString &path) const;

    const std::vector<int>& playOrder() const { return m_playOrder; }
    const QList<Track>&     tracks()    const { return m_tracks; }

    Track current() const;

    void setTracks(const QList<Track> &tracks);
    void insertNext(const Track &track);
    void removeTrack(int position);
    void retainPaths(const QSet<QString> &paths);
    void moveTrack(int from, int to);

    void setShuffle(bool enabled);
    void jumpToPosition(int pos);
    void setIndexByPath(const QString &path);

    Track next();
    Track previous();

private:
    void rebuildPlayOrder();
    void rebuildPathIndex();

    QList<Track> m_tracks;
    std::vector<int> m_playOrder;
    QHash<QString, int> m_pathToGlobalId;
    int m_currentIndex = -1;
    int m_detachedPosition = -1;
    bool m_currentDetached = false;
    bool m_shuffle = false;
};
