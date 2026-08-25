#include "JobListModel.h"

namespace TtvStudio::Core {

JobListModel::JobListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int JobListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

QVariant JobListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_records.size())
        return {};

    const Jobs::JobRecord &record = m_records.at(index.row());
    switch (role) {
    case IdRole:
        return record.id;
    case KindRole:
        return Jobs::kindToString(record.kind);
    case StateRole:
        return Jobs::stateToString(record.state);
    case CreatedAtMsRole:
        return record.createdAtMs;
    case UpdatedAtMsRole:
        return record.updatedAtMs;
    }
    return {};
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {
        {IdRole, "jobId"},
        {KindRole, "kind"},
        {StateRole, "state"},
        {CreatedAtMsRole, "createdAtMs"},
        {UpdatedAtMsRole, "updatedAtMs"},
    };
}

void JobListModel::refresh(QVector<Jobs::JobRecord> records)
{
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
}

} // namespace TtvStudio::Core
