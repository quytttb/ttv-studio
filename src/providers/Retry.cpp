#include "Retry.h"

#include <QRandomGenerator>
#include <QThread>

#include "ProviderError.h"

namespace TtvStudio::Providers {

qint64 backoffDelayMs(int attempt)
{
    // base = min(2^attempt, 8) seconds, attempt 1-based.
    const qint64 baseSeconds = qint64(1) << qBound(1, attempt, 3);
    // uniform(base * 0.5, base)
    const double low = baseSeconds * 0.5 * 1000.0;
    const double high = baseSeconds * 1000.0;
    return qint64(low + QRandomGenerator::system()->generateDouble() * (high - low));
}

void defaultSleep(qint64 ms)
{
    QThread::msleep(quint64(ms));
}

} // namespace TtvStudio::Providers
