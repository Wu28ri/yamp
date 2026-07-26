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
    endMoveRows();
    notifyCurrentChanged();
}

void QueueModel::remove(int position) {
    if (position < 0 || position >= m_queue->count()) return;

    beginRemoveRows({}, position, position);
    m_queue->removeTrack(position);
    endRemoveRows();
    notifyCurrentChanged();
}

void QueueModel::insertTrack(const Track &track) {
    const int insertIdx = m_queue->nextInsertionPosition();

    beginInsertRows({}, insertIdx, insertIdx);
    m_queue->insertNext(track);
    endInsertRows();
    notifyCurrentChanged();
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
    endResetModel();
    return true;
}

void QueueModel::resetAll() {
    beginResetModel();
    endResetModel();
}

void QueueModel::notifyCurrentChanged() {
    if (rowCount() == 0) return;
    emit dataChanged(index(0), index(rowCount() - 1), {IsCurrentRole});
}
