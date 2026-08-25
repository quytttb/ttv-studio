#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

namespace TtvStudio::Providers {

struct HttpRequest
{
    QByteArray method = "GET";
    QUrl url;
    QList<std::pair<QString, QString>> headers;
    QByteArray body;

    void setHeader(const QString &name, const QString &value)
    {
        for (auto &h : headers) {
            if (h.first.compare(name, Qt::CaseInsensitive) == 0) {
                h.second = value;
                return;
            }
        }
        headers.append({name, value});
    }

    QString headerValue(const QString &name) const
    {
        for (const auto &h : headers) {
            if (h.first.compare(name, Qt::CaseInsensitive) == 0)
                return h.second;
        }
        return {};
    }
};

// Result of one HTTP exchange through the transport.
// `body` is filled unless a `sinkFilePath` was supplied (then the payload is
// streamed straight into that file and `bytesReceived` counts what landed).
struct HttpResponse
{
    bool networkOk = false;      // an HTTP reply arrived (no transport error)
    bool timedOut = false;       // aborted after the caller's timeout
    int statusCode = 0;
    QByteArray body;             // empty when streamed to a sink file
    qint64 bytesReceived = 0;
    QString errorText;           // network-layer error text (redacted by caller)

    bool statusOk() const { return networkOk && !timedOut && statusCode >= 200 && statusCode < 300; }
};

// Blocking HTTP transport boundary. Implementations must be safe to call from
// a worker thread and enforce the per-request timeout themselves.
class ITransport
{
public:
    virtual ~ITransport() = default;

    // When `sinkFilePath` is non-empty the response body is written into that
    // file incrementally instead of being buffered in memory. The sink file is
    // removed on failure or timeout.
    virtual HttpResponse send(const HttpRequest &request,
                              int timeoutMs,
                              qint64 maxBodyBytes,
                              const QString &sinkFilePath = {}) = 0;
};

} // namespace TtvStudio::Providers
