// Blocking HTTPS GET via WinHTTP. Only ever called from worker threads.
#pragma once
#include <string>

namespace deskskin::measures {

// Returns true on HTTP 200 and fills `body` (capped at maxBytes). `error` receives a short reason on failure.
bool HttpGet(const std::wstring& url, std::string& body, std::string& error, unsigned timeoutMs = 8000,
             std::size_t maxBytes = 1u << 20);

}  // namespace deskskin::measures
