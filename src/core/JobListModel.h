#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "jobs/JobRecord.h"

namespace TtvStudio::Core {

// Read-only list of persisted jobs for the QML pages. Refreshed wholesale
// after store mutations — job counts are small, simplicity wins.
class JobListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        KindRole,
        StateRole,
        CreatedAtMsRole,
        UpdatedAtMsRole
    };

    explicit JobListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh(QVector<Jobs::JobRecord> records);

private:
    QVector<Jobs::JobRecord> m_records;
};

} // namespace TtvStudio::Core
