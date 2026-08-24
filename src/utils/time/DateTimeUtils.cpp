#include "utils/time/DateTimeUtils.h"

#include <QMetaType>
#include <QTimeZone>

namespace TtvStudio::Utils {

QDateTime parseUtc(const QString &iso)
{
    if (iso.isEmpty()) {
        return {};
    }
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(iso, Qt::ISODate);
    }
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime) {
        dt.setTimeZone(QTimeZone::UTC);
    }
    return dt;
}

QString isoUtc(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODateWithMs);
}

QVariant isoUtcOrNull(const QDateTime &dt)
{
    return dt.isValid() ? QVariant(isoUtc(dt)) : QVariant(QMetaType(QMetaType::QString));
}

} // namespace TtvStudio::Utils
