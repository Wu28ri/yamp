#include "QueueModel.h"

QueueModel::QueueModel(TrackQueue *queue, QObject *parent)
    : QAbstractListModel(parent), m_queue(queue) {}

int QueueModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_queue->count();
}

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_queue->count()) return {};

    const int trackId = m_queue->playOrder()[index.row()];
    const Track &t = m_queue->tracks()[trackId];

    switch (role) {
    case TitleRole:     return t.title;
    case ArtistRole:    return t.artist;
    case PathRole:      return t.path;
    case IsCurrentRole: return index.row() == m_queue->currentPosition();
    default:            break;
    }
    return {};
}

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {
        {TitleRole,     "title"},
        {ArtistRole,    "artist"},
        {PathRole,      "path"},
        {IsCurrentRole, "isCurrent"},
    };
}

void QueueModel::move(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= m_queue->count() || to < 0 || to >= m_queue->count()) return;

    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_queue->moveTrack(from, to);
    if (m_lastCurrentRow == from) {
        m_lastCurrentRow = to;
    } else if (from < m_lastCurrentRow && to >= m_lastCurrentRow) {
        --m_lastCurrentRow;
    } else if (from > m_lastCurrentRow && to <= m_lastCurrentRow) {
        ++m_lastCurrentRow;
    }
    endMoveRows();
    notifyCurrentChanged();
    emit queueChanged();
}

void QueueModel::remove(int position) {
    if (position < 0 || position >= m_queue->count()) return;

    beginRemoveRows({}, position, position);
    m_queue->removeTrack(position);
    if (m_lastCurrentRow == position) {
        m_lastCurrentRow = -1;
    } else if (m_lastCurrentRow > position) {
        --m_lastCurrentRow;
    }
    endRemoveRows();
    notifyCurrentChanged();
    emit queueChanged();
}

void QueueModel::insertTrack(const Track &track) {
    const int insertIdx = m_queue->nextInsertionPosition();

    beginInsertRows({}, insertIdx, insertIdx);
    m_queue->insertNext(track);
    if (m_lastCurrentRow >= insertIdx) ++m_lastCurrentRow;
    endInsertRows();
    notifyCurrentChanged();
    emit queueChanged();
}

void QueueModel::appendTrack(const Track &track) {
    const int appendIdx = m_queue->count();

    beginInsertRows({}, appendIdx, appendIdx);
    m_queue->appendTrack(track);
    endInsertRows();
    notifyCurrentChanged();
    emit queueChanged();
}

void QueueModel::clear(bool keepCurrentDetached) {
    if (m_queue->count() == 0) {
        m_queue->clear(keepCurrentDetached);
        m_lastCurrentRow = -1;
        return;
    }
    beginResetModel();
    m_queue->clear(keepCurrentDetached);
    m_lastCurrentRow = -1;
    endResetModel();
    emit queueChanged();
}

bool QueueModel::retainPaths(const QSet<QString> &paths) {
    bool hasMissingPath = false;
    for (int globalId : m_queue->playOrder()) {
        if (globalId < 0 || globalId >= m_queue->tracks().size() ||
            !paths.contains(m_queue->tracks().at(globalId).path)) {
            hasMissingPath = true;
            break;
        }
    }
    if (!hasMissingPath) return false;

    beginResetModel();
    m_queue->retainPaths(paths);
    m_lastCurrentRow = m_queue->currentPosition();
    endResetModel();
    emit queueChanged();
    return true;
}

void QueueModel::resetAll() {
    beginResetModel();
    m_lastCurrentRow = m_queue->currentPosition();
    endResetModel();
}

void QueueModel::notifyCurrentChanged() {
    const int newRow = m_queue->currentPosition();
    const int oldRow = m_lastCurrentRow;
    m_lastCurrentRow = newRow;
    if (oldRow == newRow) return;

    const int n = rowCount();
    if (oldRow >= 0 && oldRow < n)
        emit dataChanged(index(oldRow), index(oldRow), {IsCurrentRole});
    if (newRow >= 0 && newRow < n)
        emit dataChanged(index(newRow), index(newRow), {IsCurrentRole});
}
