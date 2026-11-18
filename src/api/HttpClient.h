#pragma once
#include <string>

namespace su {

struct HttpResponse {
  int status = 0;            // 0 = transport failure (see winError)
  std::string body;
  int retryAfterSec = -1;    // value of Retry-After, -1 when absent
  unsigned long winError = 0;
};

// Minimal synchronous HTTP(S) client on top of WinHTTP. Thread-safe: each call
// opens its own connection/request handles on a shared session.
class HttpClient {
 public:
  explicit HttpClient(const std::wstring& userAgent);
  ~HttpClient();
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  HttpResponse send(const std::wstring& host, const std::wstring& method, const std::wstring& pathAndQuery,
                    const std::wstring& headers, const std::string& body) const;
  // Same, on an explicit port; `secure` = false sends plain HTTP (used only for a local update server).
  HttpResponse send(const std::wstring& host, unsigned short port, bool secure, const std::wstring& method,
                    const std::wstring& pathAndQuery, const std::wstring& headers, const std::string& body) const;

 private:
  void* session_ = nullptr;  // HINTERNET
};

}  // namespace su
