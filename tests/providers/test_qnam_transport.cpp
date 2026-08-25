#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include "providers/QNamTransport.h"

using TtvStudio::Providers::HttpRequest;
using TtvStudio::Providers::HttpResponse;
using TtvStudio::Providers::QNamTransport;

namespace {

// Minimal single-connection HTTP/1.1 responder for one exchange per test.
class OneShotHttpServer : public QObject
{
    Q_OBJECT

public:
    QString response = QStringLiteral(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n"
        "Connection: close\r\n\r\n{\"answer\": 42}");
    int delayMs = 0; // stall before answering → drives the client timeout
    QByteArray receivedRequest;

    bool start()
    {
        if (!m_server.listen(QHostAddress::LocalHost))
            return false;
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_socket = m_server.nextPendingConnection();
            if (!m_socket)
                return;
            connect(m_socket, &QTcpSocket::readyRead, this, [this] {
                receivedRequest += m_socket->readAll();
                if (!receivedRequest.contains("\r\n\r\n"))
                    return;
                QTimer::singleShot(delayMs, this, [this] {
                    m_socket->write(response.toUtf8());
                    m_socket->disconnectFromHost();
                });
            });
        });
        return true;
    }

    quint16 port() const { return m_server.serverPort(); }

private:
    QTcpServer m_server;
    QTcpSocket *m_socket = nullptr;
};

} // namespace

class TestQNamTransport : public QObject
{
    Q_OBJECT

private slots:
    void parsesStatusAndBody()
    {
        OneShotHttpServer server;
        QVERIFY(server.start());

        QNamTransport transport;
        HttpRequest request;
        request.method = QByteArrayLiteral("GET");
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/api").arg(server.port()));

        const HttpResponse response = transport.send(request, 5'000, 1 << 20);

        QVERIFY(response.networkOk);
        QCOMPARE(response.statusCode, 200);
        QVERIFY(response.body.contains(QLatin1String("\"answer\": 42")));
        QVERIFY(server.receivedRequest.startsWith(QByteArrayLiteral("GET /api")));
    }

    void postsBodyWithHeader()
    {
        OneShotHttpServer server;
        QVERIFY(server.start());

        QNamTransport transport;
        HttpRequest request;
        request.method = QByteArrayLiteral("POST");
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/generate").arg(server.port()));
        request.setHeader(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
        request.body = QByteArrayLiteral("{\"x\": 1}");

        const HttpResponse response = transport.send(request, 5'000, 1 << 20);

        QVERIFY(response.networkOk);
        QVERIFY(server.receivedRequest.startsWith(QByteArrayLiteral("POST /generate")));
        QVERIFY(server.receivedRequest.contains(QByteArrayLiteral("{\"x\": 1}")));
        QVERIFY(server.receivedRequest.contains(QByteArrayLiteral("application/json")));
    }

    void timesOutWhenServerStalls()
    {
        OneShotHttpServer server;
        server.delayMs = 5'000;
        QVERIFY(server.start());

        QNamTransport transport;
        HttpRequest request;
        request.method = QByteArrayLiteral("GET");
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/slow").arg(server.port()));

        const HttpResponse response = transport.send(request, 300, 1 << 20);

        QVERIFY(!response.networkOk);
        QVERIFY(response.timedOut);
    }

    void streamsLargeBodyIntoSinkFile()
    {
        OneShotHttpServer server;
        // 2 MiB payload in the canned response.
        const QByteArray big(2 << 20, 'z');
        server.response = QStringLiteral(
                              "HTTP/1.1 200 OK\r\nContent-Length: %1\r\nConnection: close\r\n\r\n")
                              .arg(big.size())
                          + big;
        QVERIFY(server.start());

        QTemporaryDir dir;
        const QString sink = dir.filePath(QStringLiteral("payload.bin"));

        QNamTransport transport;
        HttpRequest request;
        request.method = QByteArrayLiteral("GET");
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/big").arg(server.port()));

        const HttpResponse response = transport.send(request, 10'000, 8 << 20, sink);

        QVERIFY(response.networkOk);
        QCOMPARE(response.bytesReceived, qint64(big.size()));
        QFile written(sink);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.size(), qint64(big.size()));
        QVERIFY(written.readAll() == big);
    }

    void sinkRemovedOnFailure()
    {
        OneShotHttpServer server;
        server.delayMs = 5'000; // force a timeout mid-transfer
        QVERIFY(server.start());

        QTemporaryDir dir;
        const QString sink = dir.filePath(QStringLiteral("partial.bin"));

        QNamTransport transport;
        HttpRequest request;
        request.method = QByteArrayLiteral("GET");
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/stall").arg(server.port()));

        const HttpResponse response = transport.send(request, 300, 8 << 20, sink);

        QVERIFY(!response.networkOk);
        QVERIFY(!QFile::exists(sink)); // cleaned up, no partial leftovers
    }
};

QTEST_MAIN(TestQNamTransport)
#include "test_qnam_transport.moc"
