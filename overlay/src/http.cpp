#include "overlay/http.h"

#include <winhttp.h>

#include <cstdio>

namespace pet::win {
namespace {

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

constexpr DWORD kTimeoutMs = 8000;
constexpr size_t kMaxBody = 256 * 1024;

}  // namespace

HttpClient::~HttpClient() {
    // 退出时不等线程：请求最多 8 秒超时，进程退出会把它们带走。句柄关掉。
    for (HANDLE h : threads_) CloseHandle(h);
}

unsigned HttpClient::inflight() const {
    std::lock_guard<std::mutex> lk(mu_);
    return inflight_;
}

struct ThreadArg { HttpClient* self; HttpClient::Done done; std::wstring url; };

DWORD WINAPI HttpClient::thread_main(LPVOID p) {
    auto* a = static_cast<ThreadArg*>(p);
    Job job{a->url, a->done};
    a->self->run(&job);
    delete a;
    return 0;
}

void HttpClient::get(const std::string& url, Done done) {
    auto* a = new ThreadArg{this, std::move(done), widen(url)};
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++inflight_;
    }
    HANDLE h = CreateThread(nullptr, 0, &HttpClient::thread_main, a, 0, nullptr);
    if (!h) {
        std::lock_guard<std::mutex> lk(mu_);
        --inflight_;
        ready_.push_back({std::move(a->done), false, ""});
        delete a;
        return;
    }
    threads_.push_back(h);
}

void HttpClient::run(Job* job) {
    bool ok = false;
    std::string body;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    if (WinHttpCrackUrl(job->url.c_str(), 0, 0, &uc) && uc.nScheme == INTERNET_SCHEME_HTTPS) {
        HINTERNET session = WinHttpOpen(L"CyberDog/2.0 (desktop pet; weather lookup once per launch)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) {
            WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
            HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
            if (conn) {
                HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr)) {
                    DWORD status = 0, sz = sizeof(status);
                    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < kMaxBody) {
                        std::string chunk(avail, '\0');
                        DWORD got = 0;
                        if (!WinHttpReadData(req, chunk.data(), avail, &got)) break;
                        body.append(chunk.data(), got);
                    }
                    ok = status == 200;
                }
                if (req) WinHttpCloseHandle(req);
                WinHttpCloseHandle(conn);
            }
            WinHttpCloseHandle(session);
        }
    }

    std::lock_guard<std::mutex> lk(mu_);
    --inflight_;
    ready_.push_back({std::move(job->done), ok, std::move(body)});
}

int HttpClient::drain() {
    std::vector<Result> batch;
    {
        std::lock_guard<std::mutex> lk(mu_);
        batch.swap(ready_);
    }
    for (Result& r : batch) if (r.done) r.done(r.ok, r.body);
    return static_cast<int>(batch.size());
}

}  // namespace pet::win
