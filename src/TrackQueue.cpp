#include "TrackQueue.h"

#include <algorithm>
#include <numeric>
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
    return it == m_pathToGlobalId.constEnd() ? -1 : positionOfId(it.value());
}

int TrackQueue::positionOfId(int globalId) const {
    const auto it = std::find(m_playOrder.cbegin(), m_playOrder.cend(), globalId);
    return it == m_playOrder.cend() ? -1
        : static_cast<int>(std::distance(m_playOrder.cbegin(), it));
}

void TrackQueue::setTracks(const QList<Track> &tracks) {
    if (m_tracks.size() == tracks.size() &&
        std::equal(m_tracks.cbegin(), m_tracks.cend(), tracks.cbegin(),
                   [](const Track &left, const Track &right) { return left.path == right.path; })) return;
    m_tracks = tracks;
    rebuildPathIndex();
    rebuildPlayOrder();
}

void TrackQueue::restoreState(const QList<Track> &tracks,
                              const std::vector<int> &playOrder,
                              int currentIndex, int detachedPosition,
                              bool currentDetached, bool shuffle) {
    m_tracks = tracks;
    rebuildPathIndex();

    m_playOrder.clear();
    m_playOrder.reserve(m_tracks.size());
    QSet<int> usedIds;
    for (int id : playOrder) {
        if (id < 0 || id >= m_tracks.size() || usedIds.contains(id)) continue;
        m_playOrder.push_back(id);
        usedIds.insert(id);
    }
    for (int id = 0; id < m_tracks.size(); ++id) {
        if (!usedIds.contains(id)) m_playOrder.push_back(id);
    }

    m_shuffle = shuffle;
    if (m_playOrder.empty()) {
        m_currentIndex = -1;
        m_detachedPosition = -1;
        m_currentDetached = false;
        return;
    }

    m_currentIndex = qBound(-1, currentIndex,
                            static_cast<int>(m_playOrder.size()) - 1);
    m_currentDetached = currentDetached;
    m_detachedPosition = currentDetached
        ? qBound(0, detachedPosition, static_cast<int>(m_playOrder.size()))
        : -1;
}

void TrackQueue::insertNext(const Track &track) {
    m_tracks.append(track);
    const int newTrackId = static_cast<int>(m_tracks.size()) - 1;
    if (!track.path.isEmpty()) m_pathToGlobalId.insert(track.path, newTrackId);

    const bool wasEmpty = m_playOrder.empty();
    const int insertionPosition = nextInsertionPosition();
    m_playOrder.insert(m_playOrder.begin() + insertionPosition, newTrackId);
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
        m_detachedPosition = detachedNextId < 0 ? n : positionOfId(detachedNextId);
    }
}

void TrackQueue::resetPlayOrder() {
    m_playOrder.resize(m_tracks.size());
    std::iota(m_playOrder.begin(), m_playOrder.end(), 0);
    if (m_shuffle) std::shuffle(m_playOrder.begin(), m_playOrder.end(), rng());
}

void TrackQueue::rebuildPlayOrder() {
    resetPlayOrder();
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
        resetPlayOrder();
        m_detachedPosition = nextId < 0 ? count() : positionOfId(nextId);
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

    resetPlayOrder();
    if (m_shuffle) {
        std::iter_swap(m_playOrder.begin(),
                       std::find(m_playOrder.begin(), m_playOrder.end(), globalId));
        m_currentIndex = 0;
    } else {
        m_currentIndex = globalId;
    }
    m_currentDetached = false;
    m_detachedPosition = -1;
}

Track TrackQueue::next() {
    if (m_currentDetached) {
        const int nextPosition = m_detachedPosition;
        if (nextPosition < 0 || nextPosition >= static_cast<int>(m_playOrder.size())) return {};
        jumpToPosition(nextPosition);
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
        jumpToPosition(previousPosition);
        return current();
    }
    if (m_currentIndex > 0) {
        --m_currentIndex;
    }
    return current();
}
