#include "ModbusDataDispatcher.h"

#include "network/workers/HistoryWriterWorker.h"

namespace TtvStudio::Network {

ModbusDataDispatcher::ModbusDataDispatcher(QObject *parent)
    : QObject(parent)
{
}

void ModbusDataDispatcher::onPollFinished(const PollSnapshot &snapshot)
{
    emit liveSnapshotReady(snapshot);

    if (m_historyWorker) {
        m_historyWorker->enqueue(snapshot);
    }
}

} // namespace TtvStudio::Network
