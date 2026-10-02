#pragma once

#include "model/error.h"
#include "net/httptransport.h"

namespace Harpoon {

// Ported from ObtainX GitHub.rateLimitErrorCheck. Detects rate limiting from
// x-ratelimit-remaining == 0, HTTP 403/429, or "rate limit"/"too many requests"
// in the reason phrase or the start of the body. The wait time comes from
// x-ratelimit-reset (epoch seconds), else retry-after (seconds), else 30 min.
//
// Returns an Error of kind RateLimited, or Error::None if not rate limited.
// nowMsecs is injectable for tests.
Error rateLimitError(const HttpResponse &response, qint64 nowMsecs);

} // namespace Harpoon
