#pragma once

#include <QFile>
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

        TtvStudio::Providers::HttpResponse response;
        if (script.isEmpty()) {
            response.networkOk = false;
            response.errorText = QStringLiteral("script exhausted");
            return response;
        }
        response = script.takeFirst();

        QByteArray payload = sinkPayload;
        if (sinkPayloadOverrides.contains(callIndex))
            payload = sinkPayloadOverrides.value(callIndex);

        if (!sinkFilePath.isEmpty() && response.networkOk && !payload.isEmpty()) {
            QFile sink(sinkFilePath);
            if (!sink.open(QIODevice::WriteOnly)) {
                response.networkOk = false;
                response.errorText = QStringLiteral("fake sink open failed");
                return response;
            }
            sink.write(payload);
            response.bytesReceived = payload.size();
        }
        return response;
    }
};
