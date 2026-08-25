#pragma once

#include <functional>
#include <QtGlobal>
#include <utility>

#include "ProviderError.h"

namespace TtvStudio::Providers {

// Exponential backoff with jitter, mirroring the shared provider contract:
//   base = min(2^attempt, 8) seconds; wait = uniform(base * 0.5, base)
// `attempt` is 1-based (the wait after the 1st failure uses attempt=1 →
// 1..2s; capped at 4..8s from attempt 3 onward).
qint64 backoffDelayMs(int attempt);

using SleepFn = std::function<void(qint64 ms)>;

// Default wall-clock sleep used by production callers.
void defaultSleep(qint64 ms);

// Drives a retriable operation without exceptions:
//   - invokes `op` up to `maxAttempts` times,
//   - retries only while it fails with a *Transient* error,
//     (Permanent / AmbiguousTimeout return immediately),
//   - sleeps backoffDelayMs(attempt) between attempts via `sleep`
//     (injectable so tests run instantly and deterministically).
//
// `op` must be callable as Result op() where Result has:
//   bool ok;
//   ProviderError error;   // meaningful only when !ok
template<typename Result, typename Fn>
Result retryTransient(int maxAttempts, SleepFn sleep, Fn &&op)
{
    Q_ASSERT(maxAttempts >= 1);
    Result result = op();
    for (int attempt = 1; attempt < maxAttempts && !result.ok
         && result.error.kind == ErrorKind::Transient; ++attempt) {
        if (sleep)
            sleep(backoffDelayMs(attempt));
        result = op();
    }
    return std::move(result);
}

} // namespace TtvStudio::Providers
