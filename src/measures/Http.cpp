#include "measures/Http.h"

#include <windows.h>
#include <winhttp.h>

namespace deskskin::measures {
namespace {
struct Handle {
  HINTERNET h = nullptr;
  ~Handle() { if (h) WinHttpCloseHandle(h); }
  explicit operator bool() const { return h != nullptr; }
};
}  // namespace

bool HttpGet(const std::wstring& url, std::string& body, std::string& error, unsigned timeoutMs, std::size_t maxBytes) {
  body.clear();
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = ARRAYSIZE(host);
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = ARRAYSIZE(path);
  uc.dwExtraInfoLength = static_cast<DWORD>(-1);  // ask WinHTTP to point at the query string
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = "invalid URL"; return false; }
  std::wstring target(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.lpszExtraInfo && uc.dwExtraInfoLength) target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  if (target.empty()) target = L"/";

  Handle session;
  session.h = WinHttpOpen(L"DeskSkin/0.4", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) { error = "network unavailable"; return false; }
  WinHttpSetTimeouts(session.h, static_cast<int>(timeoutMs), static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
                     static_cast<int>(timeoutMs));
  Handle conn;
  conn.h = WinHttpConnect(session.h, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0);
  if (!conn) { error = "cannot connect"; return false; }
  const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  Handle req;
  req.h = WinHttpOpenRequest(conn.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!req) { error = "cannot open request"; return false; }
  if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req.h, nullptr)) {
    error = "offline or server unreachable";
    return false;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  if (status != 200) { error = "server returned HTTP " + std::to_string(status); return false; }
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
    std::string chunk(avail, '\0');
    DWORD got = 0;
    if (!WinHttpReadData(req.h, chunk.data(), avail, &got) || got == 0) break;
    body.append(chunk.data(), got);
    if (body.size() > maxBytes) { error = "response too large"; return false; }
  }
  return !body.empty();
}

}  // namespace deskskin::measures
