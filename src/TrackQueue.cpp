#include "TrackQueue.h"

#include <algorithm>
#include <random>

namespace {
std::mt19937& rng() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}
}

Track TrackQueue::current() const {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playOrder.size()))
        return {};
    return m_tracks[m_playOrder[m_currentIndex]];
}

void TrackQueue::setTracks(const QList<Track> &tracks) {
    if (m_tracks.size() == tracks.size()) {
        bool identical = true;
        for (int i = 0; i < tracks.size(); ++i) {
            if (m_tracks[i].path != tracks[i].path) { identical = false; break; }
        }
        if (identical) return;
    }
    m_tracks = tracks;
    rebuildPathIndex();
    rebuildPlayOrder();
}

void TrackQueue::addTrack(const Track &track) {
    m_tracks.append(track);
    const int newId = static_cast<int>(m_tracks.size()) - 1;
    if (!track.path.isEmpty()) m_pathToGlobalId.insert(track.path, newId);
    m_playOrder.push_back(newId);
    if (m_currentIndex == -1) m_currentIndex = 0;
}

void TrackQueue::insertNext(const Track &track) {
    m_tracks.append(track);
    const int newTrackId = static_cast<int>(m_tracks.size()) - 1;
    if (!track.path.isEmpty()) m_pathToGlobalId.insert(track.path, newTrackId);

    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playOrder.size())) {
        m_playOrder.insert(m_playOrder.begin() + m_currentIndex + 1, newTrackId);
    } else {
        m_playOrder.push_back(newTrackId);
        m_currentIndex = 0;
    }
}

void TrackQueue::removeTrack(int position) {
    if (position < 0 || position >= static_cast<int>(m_playOrder.size())) return;

    const int globalId = m_playOrder[position];
    m_playOrder.erase(m_playOrder.begin() + position);

    if (globalId >= 0 && globalId < m_tracks.size()) {
        const QString path = m_tracks[globalId].path;
        m_tracks.removeAt(globalId);
        if (!path.isEmpty()) m_pathToGlobalId.remove(path);

        for (int &id : m_playOrder) {
            if (id > globalId) --id;
        }
        for (auto it = m_pathToGlobalId.begin(); it != m_pathToGlobalId.end(); ++it) {
            if (it.value() > globalId) --it.value();
        }
    }

    if (m_playOrder.empty()) {
        m_currentIndex = -1;
        return;
    }
    if (position < m_currentIndex) {
        --m_currentIndex;
    } else if (position == m_currentIndex
               && m_currentIndex >= static_cast<int>(m_playOrder.size())) {
        m_currentIndex = static_cast<int>(m_playOrder.size()) - 1;
    }
}

void TrackQueue::moveTrack(int from, int to) {
    const int n = static_cast<int>(m_playOrder.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    const int trackId = m_playOrder[from];
    m_playOrder.erase(m_playOrder.begin() + from);
    m_playOrder.insert(m_playOrder.begin() + to, trackId);

    if (m_currentIndex == from) {
        m_currentIndex = to;
    } else if (from < m_currentIndex && to >= m_currentIndex) {
        --m_currentIndex;
    } else if (from > m_currentIndex && to <= m_currentIndex) {
        ++m_currentIndex;
    }
}

void TrackQueue::rebuildPlayOrder() {
    m_playOrder.clear();
    m_playOrder.reserve(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i) m_playOrder.push_back(i);

    if (m_shuffle && !m_playOrder.empty()) {
        std::shuffle(m_playOrder.begin(), m_playOrder.end(), rng());
    }
    m_currentIndex = m_playOrder.empty() ? -1 : 0;
}

void TrackQueue::rebuildPathIndex() {
    m_pathToGlobalId.clear();
    m_pathToGlobalId.reserve(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i) {
        const QString &p = m_tracks[i].path;
        if (!p.isEmpty()) m_pathToGlobalId.insert(p, i);
    }
}

void TrackQueue::setShuffle(bool enabled) {
    if (m_shuffle == enabled) return;
    m_shuffle = enabled;

    const Track cur = current();
    if (cur.isValid()) {
        setIndexByPath(cur.path);
    } else {
        rebuildPlayOrder();
    }
}

void TrackQueue::jumpToPosition(int pos) {
    if (pos >= 0 && pos < static_cast<int>(m_playOrder.size())) m_currentIndex = pos;
}

void TrackQueue::setIndexByPath(const QString &path) {
    const auto it = m_pathToGlobalId.constFind(path);
    if (it == m_pathToGlobalId.constEnd()) return;
    const int globalId = it.value();

    m_playOrder.clear();
    m_playOrder.reserve(m_tracks.size());

    if (m_shuffle) {
        m_playOrder.push_back(globalId);
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (i != globalId) m_playOrder.push_back(i);
        }
        if (m_playOrder.size() > 1) {
            std::shuffle(m_playOrder.begin() + 1, m_playOrder.end(), rng());
        }
        m_currentIndex = 0;
    } else {
        for (int i = 0; i < m_tracks.size(); ++i) m_playOrder.push_back(i);
        m_currentIndex = globalId;
    }
}

Track TrackQueue::next() {
    if (m_playOrder.empty()) return {};
    if (m_repeatMode == RepeatOne) return current();

    if (m_currentIndex < static_cast<int>(m_playOrder.size()) - 1) {
        ++m_currentIndex;
    } else if (m_repeatMode == RepeatAll) {
        m_currentIndex = 0;
    }
    return current();
}

Track TrackQueue::previous() {
    if (m_playOrder.empty()) return {};
    if (m_currentIndex > 0) {
        --m_currentIndex;
    } else if (m_repeatMode == RepeatAll) {
        m_currentIndex = static_cast<int>(m_playOrder.size()) - 1;
    }
    return current();
}
