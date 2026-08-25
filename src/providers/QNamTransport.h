#pragma once

#include <QObject>

#include "Transport.h"

class QNetworkAccessManager;

namespace TtvStudio::Providers {

// Production ITransport over QNetworkAccessManager.
//
// Blocking: send() spins a local QEventLoop until the reply finishes or the
// caller's timeout elapses (then the reply is aborted and timedOut is set).
// Pipeline stages are expected to call it from worker threads only — one
// transport instance per thread (QNetworkAccessManager affinity).
class QNamTransport final : public QObject, public ITransport
{
    Q_OBJECT

public:
    explicit QNamTransport(QObject *parent = nullptr);
    ~QNamTransport() override;

    HttpResponse send(const HttpRequest &request,
                      int timeoutMs,
                      qint64 maxBodyBytes,
                      const QString &sinkFilePath = {}) override;

private:
    QNetworkAccessManager *m_manager = nullptr;
};

} // namespace TtvStudio::Providers
