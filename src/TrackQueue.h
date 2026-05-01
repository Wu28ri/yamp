#pragma once

#include "Track.h"

#include <QList>
#include <vector>

class TrackQueue {
public:
    enum RepeatMode { NoRepeat, RepeatOne, RepeatAll };

    int  currentPosition() const { return m_currentIndex; }
    int  count()           const { return static_cast<int>(m_playOrder.size()); }
    bool isShuffle()       const { return m_shuffle; }

    int  currentGlobalId() const {
        if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playOrder.size())) return -1;
        return m_playOrder[m_currentIndex];
    }

    const std::vector<int>& playOrder() const { return m_playOrder; }
    const QList<Track>&     tracks()    const { return m_tracks; }

    Track current() const;

    void setTracks(const QList<Track> &tracks);
    void addTrack(const Track &track);
    void insertNext(const Track &track);
    void removeTrack(int position);
    void moveTrack(int from, int to);

    void setShuffle(bool enabled);
    void setRepeatMode(RepeatMode mode) { m_repeatMode = mode; }
    void jumpToPosition(int pos);
    void setIndexByPath(const QString &path);

    Track next();
    Track previous();

private:
    void rebuildPlayOrder();

    QList<Track> m_tracks;
    std::vector<int> m_playOrder;
    int m_currentIndex = -1;
    bool m_shuffle = false;
    RepeatMode m_repeatMode = NoRepeat;
};
