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
    if (m_currentDetached) return {};
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playOrder.size()))
        return {};
    const int globalId = m_playOrder[m_currentIndex];
    if (globalId < 0 || globalId >= m_tracks.size()) return {};
    return m_tracks[globalId];
}

bool TrackQueue::containsPath(const QString &path) const {
    return !path.isEmpty() && m_pathToGlobalId.contains(path);
}

int TrackQueue::nextInsertionPosition() const {
    if (m_currentDetached)
        return qBound(0, m_detachedPosition, static_cast<int>(m_playOrder.size()));
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playOrder.size()))
        return m_currentIndex + 1;
    return static_cast<int>(m_playOrder.size());
}

int TrackQueue::positionOfPath(const QString &path) const {
    const auto it = m_pathToGlobalId.constFind(path);
    if (it == m_pathToGlobalId.constEnd()) return -1;
    const int globalId = it.value();
    for (size_t i = 0; i < m_playOrder.size(); ++i) {
        if (m_playOrder[i] == globalId) return static_cast<int>(i);
    }
    return -1;
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

void TrackQueue::insertNext(const Track &track) {
    m_tracks.append(track);
    const int newTrackId = static_cast<int>(m_tracks.size()) - 1;
    if (!track.path.isEmpty()) m_pathToGlobalId.insert(track.path, newTrackId);

    const bool wasEmpty = m_playOrder.empty();
    const int insertionPosition = nextInsertionPosition();
    if (insertionPosition >= 0 && insertionPosition <= static_cast<int>(m_playOrder.size())) {
        m_playOrder.insert(m_playOrder.begin() + insertionPosition, newTrackId);
    } else {
        m_playOrder.push_back(newTrackId);
        m_currentIndex = 0;
    }
    if (wasEmpty) m_currentIndex = 0;
}

void TrackQueue::removeTrack(int position) {
    if (position < 0 || position >= static_cast<int>(m_playOrder.size())) return;

    const bool removedCurrent = !m_currentDetached && position == m_currentIndex;
    if (m_currentDetached && position < m_detachedPosition) --m_detachedPosition;
    const int globalId = m_playOrder[position];
    m_playOrder.erase(m_playOrder.begin() + position);

    if (globalId >= 0 && globalId < m_tracks.size()) {
        m_tracks.removeAt(globalId);

        for (int &id : m_playOrder) {
            if (id > globalId) --id;
        }
        rebuildPathIndex();
    }

    if (m_playOrder.empty()) {
        m_currentIndex = -1;
        m_currentDetached = false;
        m_detachedPosition = -1;
        return;
    }
    if (position < m_currentIndex) {
        --m_currentIndex;
    } else if (position == m_currentIndex
               && m_currentIndex >= static_cast<int>(m_playOrder.size())) {
        m_currentIndex = static_cast<int>(m_playOrder.size()) - 1;
    }
    if (removedCurrent) {
        m_currentDetached = true;
        m_detachedPosition = position;
    }
}

void TrackQueue::retainPaths(const QSet<QString> &paths) {
    for (int position = count() - 1; position >= 0; --position) {
        const int globalId = m_playOrder[static_cast<size_t>(position)];
        if (globalId < 0 || globalId >= m_tracks.size() ||
            !paths.contains(m_tracks.at(globalId).path)) {
            removeTrack(position);
        }
    }
}

void TrackQueue::moveTrack(int from, int to) {
    const int n = static_cast<int>(m_playOrder.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    const int detachedNextId = m_currentDetached && m_detachedPosition >= 0 &&
                                       m_detachedPosition < n
                                   ? m_playOrder[m_detachedPosition] : -1;
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
    if (m_currentDetached) {
        if (detachedNextId < 0) {
            m_detachedPosition = n;
        } else {
            for (int i = 0; i < n; ++i) {
                if (m_playOrder[i] == detachedNextId) {
                    m_detachedPosition = i;
                    break;
                }
            }
        }
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
    m_currentDetached = false;
    m_detachedPosition = -1;
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
    if (m_currentDetached) {
        const int nextId = m_detachedPosition >= 0 &&
                                   m_detachedPosition < static_cast<int>(m_playOrder.size())
                               ? m_playOrder[m_detachedPosition] : -1;
        m_shuffle = enabled;
        m_playOrder.clear();
        for (int i = 0; i < m_tracks.size(); ++i) m_playOrder.push_back(i);
        if (m_shuffle) std::shuffle(m_playOrder.begin(), m_playOrder.end(), rng());
        m_detachedPosition = static_cast<int>(m_playOrder.size());
        if (nextId >= 0) {
            for (int i = 0; i < static_cast<int>(m_playOrder.size()); ++i) {
                if (m_playOrder[i] == nextId) {
                    m_detachedPosition = i;
                    break;
                }
            }
        }
        return;
    }
    m_shuffle = enabled;

    const Track cur = current();
    if (cur.isValid()) {
        setIndexByPath(cur.path);
    } else {
        rebuildPlayOrder();
    }
}

void TrackQueue::jumpToPosition(int pos) {
    if (pos >= 0 && pos < static_cast<int>(m_playOrder.size())) {
        m_currentIndex = pos;
        m_currentDetached = false;
        m_detachedPosition = -1;
    }
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
    m_currentDetached = false;
    m_detachedPosition = -1;
}

Track TrackQueue::next() {
    if (m_currentDetached) {
        const int nextPosition = m_detachedPosition;
        if (nextPosition < 0 || nextPosition >= static_cast<int>(m_playOrder.size())) return {};
        m_currentDetached = false;
        m_detachedPosition = -1;
        m_currentIndex = nextPosition;
        return current();
    }
    if (m_currentIndex >= static_cast<int>(m_playOrder.size()) - 1) return {};
    ++m_currentIndex;
    return current();
}

Track TrackQueue::previous() {
    if (m_playOrder.empty()) return {};
    if (m_currentDetached) {
        const int previousPosition = m_detachedPosition - 1;
        if (previousPosition < 0) return {};
        m_currentDetached = false;
        m_detachedPosition = -1;
        m_currentIndex = previousPosition;
        return current();
    }
    if (m_currentIndex > 0) {
        --m_currentIndex;
    }
    return current();
}
