#include "api/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <memory>

namespace su {

namespace {
struct HandleCloser {
  void operator()(HINTERNET h) const {
    if (h) WinHttpCloseHandle(h);
  }
};
using Handle = std::unique_ptr<void, HandleCloser>;
}  // namespace

HttpClient::HttpClient(const std::wstring& userAgent) {
  HINTERNET s = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  if (s) {
    WinHttpSetTimeouts(s, 10000, 15000, 30000, 60000);
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
  }
  session_ = s;
}

HttpClient::~HttpClient() {
  if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

HttpResponse HttpClient::send(const std::wstring& host, const std::wstring& method, const std::wstring& path,
                              const std::wstring& headers, const std::string& body) const {
  return send(host, INTERNET_DEFAULT_HTTPS_PORT, true, method, path, headers, body);
}

HttpResponse HttpClient::send(const std::wstring& host, unsigned short port, bool secure, const std::wstring& method,
                              const std::wstring& path, const std::wstring& headers, const std::string& body) const {
  HttpResponse r;
  if (!session_) {
    r.winError = ERROR_INVALID_HANDLE;
    return r;
  }
  Handle connect(WinHttpConnect(static_cast<HINTERNET>(session_), host.c_str(), port, 0));
  if (!connect) {
    r.winError = GetLastError();
    return r;
  }
  Handle request(WinHttpOpenRequest(connect.get(), method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request) {
    r.winError = GetLastError();
    return r;
  }

  LPVOID data = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
  DWORD len = static_cast<DWORD>(body.size());
  if (!WinHttpSendRequest(request.get(), headers.c_str(), static_cast<DWORD>(-1L), data, len, len, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    r.winError = GetLastError();
    return r;
  }

  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  r.status = static_cast<int>(status);

  wchar_t retry[64] = {};
  DWORD retrySize = sizeof(retry);
  if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &retrySize,
                          WINHTTP_NO_HEADER_INDEX)) {
    r.retryAfterSec = _wtoi(retry);
  }

  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      r.winError = GetLastError();
      r.status = 0;
      return r;
    }
    if (available == 0) break;
    size_t offset = r.body.size();
    r.body.resize(offset + available);
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), r.body.data() + offset, available, &read)) {
      r.winError = GetLastError();
      r.status = 0;
      return r;
    }
    r.body.resize(offset + read);
  }
  return r;
}

}  // namespace su
