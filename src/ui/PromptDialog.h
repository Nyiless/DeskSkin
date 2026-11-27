// A tiny modal "type a value" dialog (used for text options such as city or feed URL, and for search).
#pragma once
#include <string>

#include "platform/DesktopHost.h"

namespace deskskin::ui {

// Returns true if the user pressed OK; `value` is the initial text on entry and the result on success.
bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value);

}  // namespace deskskin::ui
