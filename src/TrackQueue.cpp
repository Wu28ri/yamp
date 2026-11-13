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
    QList<Track> deduped;
    deduped.reserve(tracks.size());
    QSet<QString> seenPaths;
    seenPaths.reserve(tracks.size());
    for (const Track &t : tracks) {
        if (!t.path.isEmpty()) {
            if (seenPaths.contains(t.path)) continue;
            seenPaths.insert(t.path);
        }
        deduped.append(t);
    }

    if (m_tracks.size() == deduped.size() &&
        static_cast<int>(m_playOrder.size()) == deduped.size() &&
        std::equal(m_tracks.cbegin(), m_tracks.cend(), deduped.cbegin(),
                   [](const Track &left, const Track &right) { return left.path == right.path; })) {
        return;
    }
    m_tracks = std::move(deduped);
    rebuildPathIndex();
    rebuildPlayOrder();
}

void TrackQueue::restoreState(const QList<Track> &tracks,
                              const std::vector<int> &playOrder,
                              int currentIndex, int detachedPosition,
                              bool currentDetached, bool shuffle) {
    m_tracks.clear();
    m_tracks.reserve(tracks.size());
    m_pathToGlobalId.clear();
    m_pathToGlobalId.reserve(tracks.size());

    std::vector<int> oldToNewId(static_cast<size_t>(tracks.size()), -1);
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &t = tracks[i];
        if (!t.path.isEmpty()) {
            const auto existing = m_pathToGlobalId.constFind(t.path);
            if (existing != m_pathToGlobalId.constEnd()) {
                oldToNewId[static_cast<size_t>(i)] = existing.value();
                continue;
            }
            const int newId = static_cast<int>(m_tracks.size());
            m_tracks.append(t);
            m_pathToGlobalId.insert(t.path, newId);
            oldToNewId[static_cast<size_t>(i)] = newId;
        } else {
            const int newId = static_cast<int>(m_tracks.size());
            m_tracks.append(t);
            oldToNewId[static_cast<size_t>(i)] = newId;
        }
    }

    m_playOrder.clear();
    m_playOrder.reserve(std::max(playOrder.size(), static_cast<size_t>(m_tracks.size())));
    QSet<int> usedIds;
    usedIds.reserve(m_tracks.size());
    for (int oldId : playOrder) {
        if (oldId < 0 || oldId >= tracks.size()) continue;
        const int newId = oldToNewId[static_cast<size_t>(oldId)];
        if (newId < 0 || newId >= m_tracks.size()) continue;
        m_playOrder.push_back(newId);
        usedIds.insert(newId);
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

int TrackQueue::ensureTrackStored(const Track &track) {
    if (!track.path.isEmpty()) {
        const auto it = m_pathToGlobalId.constFind(track.path);
        if (it != m_pathToGlobalId.constEnd()) {
            const int existingId = it.value();
            if (existingId >= 0 && existingId < m_tracks.size()) {
                m_tracks[existingId] = track;
                return existingId;
            }
        }
    }
    m_tracks.append(track);
    const int newTrackId = static_cast<int>(m_tracks.size()) - 1;
    if (!track.path.isEmpty()) m_pathToGlobalId.insert(track.path, newTrackId);
    return newTrackId;
}

void TrackQueue::insertNext(const Track &track) {
    const int trackId = ensureTrackStored(track);
    const bool wasEmpty = m_playOrder.empty();
    const int insertionPosition = nextInsertionPosition();
    m_playOrder.insert(m_playOrder.begin() + insertionPosition, trackId);
    if (!m_currentDetached) {
        if (wasEmpty) {
            m_currentIndex = 0;
        } else if (insertionPosition <= m_currentIndex) {
            ++m_currentIndex;
        }
    }
}

void TrackQueue::appendTrack(const Track &track) {
    const int trackId = ensureTrackStored(track);
    const bool wasEmpty = m_playOrder.empty();
    m_playOrder.push_back(trackId);
    if (wasEmpty && !m_currentDetached) {
        m_currentIndex = 0;
    }
}

void TrackQueue::removeTrack(int position) {
    if (position < 0 || position >= static_cast<int>(m_playOrder.size())) return;

    const bool removedCurrent = !m_currentDetached && position == m_currentIndex;
    if (m_currentDetached && position < m_detachedPosition) --m_detachedPosition;
    const int globalId = m_playOrder[position];
    m_playOrder.erase(m_playOrder.begin() + position);

    const bool stillReferenced = std::find(m_playOrder.cbegin(), m_playOrder.cend(), globalId) != m_playOrder.cend();
    if (!stillReferenced && globalId >= 0 && globalId < m_tracks.size()) {
        m_tracks.removeAt(globalId);

        for (int &id : m_playOrder) {
            if (id > globalId) --id;
        }
        rebuildPathIndex();
    }

    if (m_playOrder.empty()) {
        m_currentIndex = -1;
        if (removedCurrent || m_currentDetached) {
            m_currentDetached = true;
            m_detachedPosition = 0;
        } else {
            m_currentDetached = false;
            m_detachedPosition = -1;
        }
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

void TrackQueue::clear(bool keepCurrentDetached) {
    m_tracks.clear();
    m_playOrder.clear();
    m_pathToGlobalId.clear();
    m_currentIndex = -1;
    m_currentDetached = keepCurrentDetached;
    m_detachedPosition = keepCurrentDetached ? 0 : -1;
}

void TrackQueue::retainPaths(const QSet<QString> &paths) {
    if (m_playOrder.empty()) {
        if (!m_tracks.isEmpty()) {
            m_tracks.clear();
            m_pathToGlobalId.clear();
        }
        return;
    }

    QList<Track> newTracks;
    newTracks.reserve(m_tracks.size());
    std::vector<int> oldToNewId(static_cast<size_t>(m_tracks.size()), -1);
    std::vector<int> newPlayOrder;
    newPlayOrder.reserve(m_playOrder.size());

    const int oldCount = static_cast<int>(m_playOrder.size());
    const bool wasDetached = m_currentDetached;
    const int oldCurrent = m_currentIndex;
    const int oldDetachedPos = m_detachedPosition;

    int keptBeforeCurrent = 0;
    int keptBeforeDetached = 0;
    bool currentKept = false;

    for (int pos = 0; pos < oldCount; ++pos) {
        const int oldId = m_playOrder[static_cast<size_t>(pos)];
        const bool keep = oldId >= 0 && oldId < m_tracks.size()
            && paths.contains(m_tracks.at(oldId).path);

        if (keep) {
            if (pos < oldCurrent) ++keptBeforeCurrent;
            if (wasDetached && pos < oldDetachedPos) ++keptBeforeDetached;
            if (!wasDetached && pos == oldCurrent) currentKept = true;

            int newId = oldToNewId[static_cast<size_t>(oldId)];
            if (newId < 0) {
                newId = static_cast<int>(newTracks.size());
                newTracks.append(m_tracks.at(oldId));
                oldToNewId[static_cast<size_t>(oldId)] = newId;
            }
            newPlayOrder.push_back(newId);
        }
    }

    m_tracks = std::move(newTracks);
    m_playOrder = std::move(newPlayOrder);
    rebuildPathIndex();

    const int newCount = static_cast<int>(m_playOrder.size());
    if (newCount == 0) {
        const bool hadActive = wasDetached || (oldCurrent >= 0 && oldCurrent < oldCount);
        m_currentIndex = -1;
        m_currentDetached = hadActive;
        m_detachedPosition = hadActive ? 0 : -1;
        return;
    }

    if (wasDetached) {
        m_currentDetached = true;
        m_detachedPosition = qBound(0, keptBeforeDetached, newCount);
        m_currentIndex = oldCurrent < 0 ? -1 : std::min(keptBeforeCurrent, newCount - 1);
    } else if (oldCurrent >= 0 && oldCurrent < oldCount) {
        if (currentKept) {
            m_currentDetached = false;
            m_detachedPosition = -1;
            m_currentIndex = keptBeforeCurrent;
        } else {
            m_currentDetached = true;
            m_detachedPosition = qBound(0, keptBeforeCurrent, newCount);
            m_currentIndex = std::min(keptBeforeCurrent, newCount - 1);
        }
    } else {
        m_currentIndex = -1;
        m_currentDetached = false;
        m_detachedPosition = -1;
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

Track TrackQueue::peekNext(bool wrapAround) const {
    if (m_playOrder.empty()) return {};
    int nextPos = -1;
    if (m_currentDetached) {
        if (m_detachedPosition >= 0 && m_detachedPosition < static_cast<int>(m_playOrder.size())) {
            nextPos = m_detachedPosition;
        } else if (wrapAround) {
            nextPos = 0;
        }
    } else if (m_currentIndex < 0) {
        nextPos = 0;
    } else if (m_currentIndex + 1 < static_cast<int>(m_playOrder.size())) {
        nextPos = m_currentIndex + 1;
    } else if (wrapAround) {
        nextPos = 0;
    }
    if (nextPos < 0 || nextPos >= static_cast<int>(m_playOrder.size())) return {};
    const int globalId = m_playOrder[static_cast<size_t>(nextPos)];
    if (globalId < 0 || globalId >= m_tracks.size()) return {};
    return m_tracks[globalId];
}

bool TrackQueue::hasPrevious(bool wrapAround) const {
    if (m_playOrder.empty()) return false;
    if (wrapAround) return true;
    if (m_currentDetached) return m_detachedPosition > 0;
    return m_currentIndex > 0;
}

Track TrackQueue::next(bool wrapAround) {
    if (m_playOrder.empty()) return {};
    if (m_currentDetached) {
        int nextPosition = m_detachedPosition;
        if (nextPosition < 0 || nextPosition >= static_cast<int>(m_playOrder.size())) {
            if (!wrapAround) return {};
            nextPosition = 0;
        }
        jumpToPosition(nextPosition);
        return current();
    }
    if (m_currentIndex + 1 < static_cast<int>(m_playOrder.size())) {
        ++m_currentIndex;
        return current();
    }
    if (wrapAround) {
        m_currentIndex = 0;
        return current();
    }
    return {};
}

Track TrackQueue::previous(bool wrapAround) {
    if (m_playOrder.empty()) return {};
    if (m_currentDetached) {
        int previousPosition = m_detachedPosition - 1;
        if (previousPosition < 0) {
            if (!wrapAround) return {};
            previousPosition = static_cast<int>(m_playOrder.size()) - 1;
        }
        jumpToPosition(previousPosition);
        return current();
    }
    if (m_currentIndex > 0) {
        --m_currentIndex;
        return current();
    }
    if (wrapAround) {
        m_currentIndex = static_cast<int>(m_playOrder.size()) - 1;
        return current();
    }
    return {};
}
