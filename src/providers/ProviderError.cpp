#include "ProviderError.h"

#include <QRegularExpression>

namespace TtvStudio::Providers {

namespace {

// (?i)(authorization\s*[:=]\s*)bearer\s+[^\s,"']+
const QRegularExpression &authHeaderPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("(authorization\\s*[:=]\\s*)bearer\\s+[^\\s,\"']+"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// \bbearer\s+[A-Za-z0-9._\-~+/=]{8,}
const QRegularExpression &bearerTokenPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("\\bbearer\\s+[A-Za-z0-9._\\-~+/=]{8,}"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// ([?&](?:api[_-]?key|key|token|sig|signature|access_token)=)[^&\s"']
// Group 1 stops right after '=' so the replacement drops the secret value.
const QRegularExpression &urlTokenParamPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("([?&](?:api[_-]?key|key|token|sig|signature|access_token)=)[^&\\s\"']+"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

} // namespace

QString redactMessage(const QString &message)
{
    QString redacted = message;
    redacted.replace(authHeaderPattern(), QStringLiteral("\\1[REDACTED]"));
    redacted.replace(bearerTokenPattern(), QStringLiteral("Bearer [REDACTED]"));
    redacted.replace(urlTokenParamPattern(), QStringLiteral("\\1[REDACTED]"));
    return redacted;
}

QString redactValue(QString message, const QString &secretValue)
{
    if (secretValue.isEmpty())
        return message;
    message.replace(secretValue, QStringLiteral("[REDACTED]"));
    return message;
}

bool isTransientHttpStatus(int statusCode)
{
    switch (statusCode) {
    case 408: // request timeout
    case 425: // too early
    case 429: // rate limited
    case 500:
    case 502:
    case 503:
    case 504:
        return true;
    default:
        return statusCode >= 500 && statusCode <= 599;
    }
}

} // namespace TtvStudio::Providers
