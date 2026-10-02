#pragma once

#include <QString>

#include <utility>

namespace Harpoon {

struct Error
{
    enum Kind {
        None,
        InvalidUrl,      // URL does not match the source's pattern
        UnsupportedUrl,  // no source handles this URL
        Network,         // connection failure, timeout
        Http,            // unexpected HTTP status
        NotFound,        // repository or project does not exist
        RateLimited,     // retry after retryAfterMinutes
        Parse,           // response could not be parsed
        NoReleases,      // source returned no releases
        NoAsset,         // no release had a matching package
        NoVersion,       // version extraction produced nothing
        InvalidSetting,  // e.g. a regex setting does not compile
    };

    Kind kind = None;
    QString message;
    int httpStatus = 0;
    int retryAfterMinutes = 0;

    bool ok() const { return kind == None; }

    static Error make(Kind kind, const QString &message)
    {
        Error e;
        e.kind = kind;
        e.message = message;
        return e;
    }
};

template <typename T>
struct Result
{
    T value{};
    Error error;

    bool ok() const { return error.ok(); }

    static Result success(T v)
    {
        Result r;
        r.value = std::move(v);
        return r;
    }
    static Result failure(Error e)
    {
        Result r;
        r.error = std::move(e);
        return r;
    }
};

} // namespace Harpoon
