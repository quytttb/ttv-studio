#include "QNamTransport.h"

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace TtvStudio::Providers {

QNamTransport::QNamTransport(QObject *parent)
    : QObject(parent),
      m_manager(new QNetworkAccessManager(this))
{
}

QNamTransport::~QNamTransport() = default;

HttpResponse QNamTransport::send(const HttpRequest &request,
                                 int timeoutMs,
                                 qint64 maxBodyBytes,
                                 const QString &sinkFilePath)
{
    HttpResponse response;

    QNetworkRequest networkRequest(request.url);
    // No QNetworkRequest::setTransferTimeout here: the caller-owned QTimer
    // below is the single timeout authority, so timeouts are always reported
    // as timedOut (ambiguous) rather than as a generic transport error.
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &[name, value] : request.headers)
        networkRequest.setRawHeader(name.toUtf8(), value.toUtf8());

    QFile sink;
    if (!sinkFilePath.isEmpty()) {
        sink.setFileName(sinkFilePath);
        if (!sink.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            response.errorText = QStringLiteral("cannot open sink file: %1").arg(sinkFilePath);
            return response;
        }
    }

    QNetworkReply *reply =
        request.method == QByteArrayLiteral("GET")
            ? m_manager->get(networkRequest)
            : m_manager->sendCustomRequest(networkRequest, request.method, request.body);

    bool timedOut = false;
    bool tooLarge = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // Incremental accounting only matters when streaming into a sink file:
    // that path enforces the byte cap while chunks arrive. In-memory bodies
    // are capped after the loop (QNAM buffers them internally regardless).
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        if (!sink.isOpen())
            return;
        const QByteArray chunk = reply->readAll();
        response.bytesReceived += chunk.size();
        sink.write(chunk);
        if (response.bytesReceived > maxBodyBytes) {
            tooLarge = true;
            reply->abort();
            loop.quit();
        }
    });

    timer.start(timeoutMs);
    loop.exec();
    timer.stop();

    // Drain whatever QNAM still buffers (readyRead is not guaranteed to fire
    // for every chunk before finished), then flush the sink.
    if (sink.isOpen()) {
        const QByteArray remainder = reply->readAll();
        response.bytesReceived += remainder.size();
        sink.write(remainder);
        sink.close();
    }

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusAttr.isValid())
        response.statusCode = statusAttr.toInt();
    response.timedOut = timedOut;

    // Servers using "Connection: close" may surface a benign closed-connection
    // error even when the exchange completed — tolerate it when a status line
    // arrived and the transfer finished on its own (not aborted by us).
    const bool benignClose =
        reply->error() != QNetworkReply::NoError && !timedOut && !tooLarge
        && statusAttr.isValid()
        && (reply->error() == QNetworkReply::RemoteHostClosedError
            || reply->error() == QNetworkReply::ProxyConnectionClosedError);

    if (timedOut) {
        response.errorText = QStringLiteral("request timed out after %1 ms").arg(timeoutMs);
    } else if (tooLarge) {
        response.errorText = QStringLiteral("response body exceeds %1 bytes").arg(maxBodyBytes);
    } else if (reply->error() != QNetworkReply::NoError && !benignClose) {
        response.errorText = reply->errorString();
    } else if (!statusAttr.isValid()) {
        response.errorText = QStringLiteral("no HTTP status line received");
    } else {
        response.networkOk = true;
        if (sinkFilePath.isEmpty()) {
            response.body = reply->readAll();
            response.bytesReceived = response.body.size();
            if (qint64(response.body.size()) > maxBodyBytes) {
                response.networkOk = false;
                response.body.clear();
                response.errorText =
                    QStringLiteral("response body exceeds %1 bytes").arg(maxBodyBytes);
            }
        }
    }

    reply->deleteLater();

    if ((!response.networkOk || tooLarge) && !sinkFilePath.isEmpty())
        QFile::remove(sinkFilePath);

    return response;
}

} // namespace TtvStudio::Providers
