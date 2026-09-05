// 异步 HTTP GET。WinHTTP，随系统提供。
//
// 只给用户明确开启的插件用（天气）。每个请求一个后台线程，结果放进队列，
// 宿主在主循环里 drain() 回调——回调永远在主线程，插件不用考虑线程。
// 超时 8 秒；只支持 https；响应最大 256 KB。

#pragma once

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pet::win {

class HttpClient {
public:
    using Done = std::function<void(bool ok, const std::string& body)>;

    ~HttpClient();
    // 发起请求。url 是 UTF-8。
    void get(const std::string& url, Done done);
    // 主循环里调：把完成的请求回调出去。返回处理了几个。
    int drain();
    unsigned inflight() const;

private:
    struct Result { Done done; bool ok; std::string body; };
    struct Job { std::wstring url; Done done; };
    static DWORD WINAPI thread_main(LPVOID p);
    void run(Job* job);

    mutable std::mutex mu_;
    std::vector<Result> ready_;
    std::vector<HANDLE> threads_;
    unsigned inflight_ = 0;
};

}  // namespace pet::win
