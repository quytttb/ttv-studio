#pragma once

#include <QFile>
#include <functional>
#include <QList>
#include <QtTest>

#include "providers/Transport.h"

// Scriptable in-memory ITransport for provider client tests: each scripted
// response is consumed in order; when the script runs dry a generic network
// failure is returned. Every request is recorded for assertions.
class FakeTransport : public TtvStudio::Providers::ITransport
{
public:
    struct Call
    {
        TtvStudio::Providers::HttpRequest request;
        int timeoutMs = 0;
        qint64 maxBodyBytes = 0;
        QString sinkPath;
    };

    QList<Call> calls;
    QList<TtvStudio::Providers::HttpResponse> script;
    // When set and the scripted response claims success, the transport writes
    // this payload into the sink file (simulating a streamed body).
    QByteArray sinkPayload;
    // Per-call override keyed by 0-based call index — lets one scripted
    // session serve different bodies per endpoint (e.g. WAV then MP4).
    QHash<int, QByteArray> sinkPayloadOverrides;
    // Debug hook: invoked with the call index and URL for every request.
    std::function<void(int, const QString &)> onCall;
    std::function<void(int, const QString &)> onResult;

    TtvStudio::Providers::HttpResponse send(
        const TtvStudio::Providers::HttpRequest &request, int timeoutMs, qint64 maxBodyBytes,
        const QString &sinkFilePath = {}) override
    {
        Call call;
        call.request = request;
        call.timeoutMs = timeoutMs;
        call.maxBodyBytes = maxBodyBytes;
        call.sinkPath = sinkFilePath;
        calls.append(call);
        const int callIndex = calls.size() - 1;
        if (onCall)
            onCall(callIndex, request.url.toString());

        TtvStudio::Providers::HttpResponse response;
        if (script.isEmpty()) {
            if (onResult) {
                onResult(callIndex,
                         QStringLiteral("EXHAUSTED at call %1").arg(callIndex));
            }
            response.networkOk = false;
            response.errorText = QStringLiteral("script exhausted");
            return response;
        }
        response = script.takeFirst();

        QByteArray payload = sinkPayload;
        QStringList dbgSinkState;
        if (sinkPayloadOverrides.contains(callIndex))
            payload = sinkPayloadOverrides.value(callIndex);

        if (!sinkFilePath.isEmpty() && response.networkOk && !payload.isEmpty()) {
            QFile sink(sinkFilePath);
            if (!sink.open(QIODevice::WriteOnly)) {
                response.networkOk = false;
                response.errorText = QStringLiteral("fake sink open failed");
            } else {
                sink.write(payload);
                response.bytesReceived = payload.size();
            }
        }
        if (onResult) {
            onResult(callIndex,
                     QStringLiteral("netOk=%1 status=%2 bytes=%3 err=%4 sink=%5")
                         .arg(response.networkOk)
                         .arg(response.statusCode)
                         .arg(response.bytesReceived)
                         .arg(response.errorText, sinkFilePath));
        }
        return response;
    }
};
