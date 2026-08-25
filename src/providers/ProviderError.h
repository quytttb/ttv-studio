#pragma once

#include <QString>

namespace TtvStudio::Providers {

// Normalized provider failure kinds (mirrors the provider error contract):
//   Transient        — retriable: timeouts, transport errors, HTTP 408/425/429/5xx
//   Permanent        — non-retriable: auth, validation, quota, malformed payload
//   AmbiguousTimeout — submit/poll timed out while remote state is unknown;
//                      callers must persist the task id and reconcile,
//                      never blindly resubmit
enum class ErrorKind { Transient, Permanent, AmbiguousTimeout };

struct ProviderError
{
    ErrorKind kind = ErrorKind::Transient;
    QString provider;      // e.g. "openai_compatible_llm"
    QString message;       // secret-safe (redacted) human-readable text
    int statusCode = 0;    // HTTP status when known, 0 otherwise

    bool isTransient() const { return kind == ErrorKind::Transient; }
    bool isAmbiguous() const { return kind == ErrorKind::AmbiguousTimeout; }
};

// Secret redaction for messages destined to logs or persisted job state.
// Strips Authorization headers, bearer tokens and URL key/token parameters.
QString redactMessage(const QString &message);

// Removes one known secret value from a message wherever it appears.
QString redactValue(QString message, const QString &secretValue);

// HTTP 408/425/429/5xx are retriable per the provider contract.
bool isTransientHttpStatus(int statusCode);

} // namespace TtvStudio::Providers
