#include "LlmClient.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include "Transport.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Providers {

namespace {

const QRegularExpression &fencedJsonPattern()
{
    // ```(json)?\s*(\{.*\})\s*```  (dotall)
    static const QRegularExpression pattern(
        QStringLiteral("```(?:json)?\\s*(\\{.*\\})\\s*```"),
        QRegularExpression::DotMatchesEverythingOption);
    return pattern;
}

// Parse model output as a JSON object, tolerating markdown fencing.
bool extractJsonObject(const QString &text, QJsonObject *out, QString *errOut)
{
    QString candidate = text.trimmed();
    if (candidate.startsWith(u"```")) {
        const auto match = fencedJsonPattern().match(text);
        if (match.hasMatch())
            candidate = match.captured(1);
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *errOut = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        *errOut = QStringLiteral("Expected a JSON object at the top level");
        return false;
    }
    *out = doc.object();
    return true;
}

QString schemaInstruction(const QString &schema)
{
    return QStringLiteral(
               "Respond with a single JSON object and nothing else; no prose, no markdown "
               "fencing. The JSON must validate against this JSON Schema exactly: %1")
        .arg(schema);
}

QVector<LlmMessage> withJsonInstructions(const QVector<LlmMessage> &messages, const QString &schema)
{
    QVector<LlmMessage> prepared = messages;
    const LlmMessage instruction{QStringLiteral("system"), schemaInstruction(schema)};
    if (!prepared.isEmpty() && prepared.first().role == QLatin1String("system"))
        prepared[0].content = instruction.content + QStringLiteral("\n\n") + prepared[0].content;
    else
        prepared.prepend(instruction);
    return prepared;
}

QVector<LlmMessage> repairMessages(const QVector<LlmMessage> &messages,
                                   const QString &brokenContent,
                                   const QString &parseError)
{
    QVector<LlmMessage> prepared = messages;
    prepared.append({QStringLiteral("assistant"), brokenContent});
    prepared.append({
        QStringLiteral("user"),
        QStringLiteral(
            "Your previous reply failed JSON schema validation with these errors:\n%1\n\n"
            "Reply again with the corrected single JSON object only.")
            .arg(parseError),
    });
    return prepared;
}

std::optional<int> sumOpt(const std::optional<int> &a, const std::optional<int> &b)
{
    if (a && b)
        return *a + *b;
    return a ? a : b;
}

// Defense in depth: strip the configured key itself from any message that
// might echo it (server bodies, transport errors), beyond generic patterns.
QString scrub(const LlmConfig &config, const QString &message)
{
    return redactValue(redactMessage(message), config.apiKey);
}

} // namespace

LlmClient::LlmClient(ITransport &transport, LlmConfig config, SleepFn sleep)
    : m_transport(transport),
      m_config(std::move(config)),
      m_sleep(std::move(sleep))
{
    if (!m_config.timeoutMs)
        m_config.timeoutMs = Defaults::kLlmTimeoutMs;
    if (!m_config.maxAttempts)
        m_config.maxAttempts = Defaults::kProviderMaxAttempts;
}

LlmCompletionResult LlmClient::complete(const QVector<LlmMessage> &messages, double temperature)
{
    if (m_config.model.trimmed().isEmpty())
        return LlmCompletionResult::failure({ErrorKind::Permanent,
                                             QLatin1String(kLlmProviderName),
                                             QStringLiteral("LLM_MODEL must be configured"),
                                             0});

    QJsonObject bodyObj;
    bodyObj.insert(QLatin1String("model"), m_config.model);
    QJsonArray messageArray;
    for (const auto &m : messages)
        messageArray.append(QJsonObject{{QLatin1String("role"), m.role},
                                        {QLatin1String("content"), m.content}});
    bodyObj.insert(QLatin1String("messages"), messageArray);
    bodyObj.insert(QLatin1String("temperature"), temperature);
    bodyObj.insert(QLatin1String("response_format"),
                   QJsonObject{{QLatin1String("type"), QStringLiteral("json_object")}});

    HttpRequest request;
    request.method = QByteArrayLiteral("POST");
    request.url = QUrl(m_config.baseUrl + QStringLiteral("/chat/completions"));
    request.setHeader(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty())
        request.setHeader(QStringLiteral("Authorization"),
                          QStringLiteral("Bearer %1").arg(m_config.apiKey));
    request.body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QElapsedTimer clock;
    clock.start();

    struct RoundResult
    {
        bool ok = false;
        QString content;
        QJsonObject usage;
        ProviderError error;
    };

    const RoundResult round = retryTransient<RoundResult>(
        m_config.maxAttempts, m_sleep, [&]() -> RoundResult {
            const HttpResponse response =
                m_transport.send(request, m_config.timeoutMs, /*maxBodyBytes*/ 16 << 20);

            if (response.timedOut) {
                return {false, {}, {},
                        {ErrorKind::Transient, QLatin1String(kLlmProviderName),
                         scrub(m_config, QStringLiteral("LLM request timed out: %1")
                                           .arg(response.errorText)),
                         response.statusCode}};
            }
            if (!response.networkOk) {
                return {false, {}, {},
                        {ErrorKind::Transient, QLatin1String(kLlmProviderName),
                         scrub(m_config, QStringLiteral("LLM transport error: %1")
                                           .arg(response.errorText)),
                         response.statusCode}};
            }
            if (isTransientHttpStatus(response.statusCode)) {
                return {false, {}, {},
                        {ErrorKind::Transient, QLatin1String(kLlmProviderName),
                         QStringLiteral("LLM endpoint returned HTTP %1").arg(response.statusCode),
                         response.statusCode}};
            }
            if (response.statusCode >= 400) {
                return {false, {}, {},
                        {ErrorKind::Permanent, QLatin1String(kLlmProviderName),
                         scrub(m_config, QStringLiteral("LLM endpoint returned HTTP %1: %2")
                                           .arg(response.statusCode)
                                           .arg(QString::fromUtf8(response.body.left(300)))),
                         response.statusCode}};
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                return {false, {}, {},
                        {ErrorKind::Permanent, QLatin1String(kLlmProviderName),
                         QStringLiteral("LLM endpoint returned malformed JSON"),
                         response.statusCode}};
            }
            const QJsonValue contentValue =
                doc.object().value(QLatin1String("choices")).toArray().at(0).toObject().value(
                    QLatin1String("message")).toObject().value(QLatin1String("content"));
            if (!contentValue.isString() || contentValue.toString().trimmed().isEmpty()) {
                return {false, {}, {},
                        {ErrorKind::Permanent, QLatin1String(kLlmProviderName),
                         QStringLiteral("LLM response is missing choices[0].message.content "
                                        "or it is empty"),
                         response.statusCode}};
            }
            return {true,
                    contentValue.toString(),
                    doc.object().value(QLatin1String("usage")).toObject(),
                    {}};
        });

    LlmCompletionResult result;
    result.latencySeconds = clock.elapsed() / 1000.0;
    result.rounds = 1;
    if (!round.ok) {
        result.error = round.error;
        return result;
    }

    result.ok = true;
    result.content = round.content;
    if (!round.usage.isEmpty()) {
        const QJsonValue p = round.usage.value(QLatin1String("prompt_tokens"));
        const QJsonValue c = round.usage.value(QLatin1String("completion_tokens"));
        const QJsonValue t = round.usage.value(QLatin1String("total_tokens"));
        if (p.isDouble())
            result.promptTokens = p.toInt();
        if (c.isDouble())
            result.completionTokens = c.toInt();
        if (t.isDouble())
            result.totalTokens = t.toInt();
    }
    return result;
}

LlmCompletionResult LlmClient::completeStructured(const QVector<LlmMessage> &messages,
                                                  const QString &jsonSchema,
                                                  double temperature)
{
    QElapsedTimer clock;
    clock.start();

    const QVector<LlmMessage> instructed = withJsonInstructions(messages, jsonSchema);
    LlmCompletionResult first = complete(instructed, temperature);
    first.latencySeconds = clock.elapsed() / 1000.0;
    if (!first.ok)
        return first;

    QJsonObject parsed;
    QString parseError;
    if (extractJsonObject(first.content, &parsed, &parseError)) {
        first.json = parsed;
        first.rounds = 1;
        return first;
    }

    // One repair round-trip; persistent malformed output fails closed as Permanent.
    LlmCompletionResult second =
        complete(repairMessages(instructed, first.content, parseError), temperature);
    second.latencySeconds = clock.elapsed() / 1000.0;
    if (!second.ok)
        return second;

    QJsonObject repaired;
    QString secondError;
    if (extractJsonObject(second.content, &repaired, &secondError)) {
        second.json = repaired;
        second.rounds = 2;
        second.promptTokens = sumOpt(second.promptTokens, first.promptTokens);
        second.completionTokens = sumOpt(second.completionTokens, first.completionTokens);
        second.totalTokens = sumOpt(second.totalTokens, first.totalTokens);
        return second;
    }

    return LlmCompletionResult::failure(
        {ErrorKind::Permanent,
         QLatin1String(kLlmProviderName),
         QStringLiteral("Structured output failed schema validation after repair "
                        "(model=%1; last error: %2)")
             .arg(m_config.model, secondError),
         0});
}

} // namespace TtvStudio::Providers
