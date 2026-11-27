#include "ui/PromptDialog.h"

namespace deskskin::ui {
namespace {

constexpr wchar_t kClass[] = L"DeskSkin.Prompt";

struct State {
  std::wstring* value = nullptr;
  HWND edit = nullptr;
  bool ok = false, done = false;
};

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_NCCREATE:
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
      break;
    case WM_COMMAND:
      if (!st) break;
      if (LOWORD(w) == IDOK) {
        wchar_t buf[2048] = {};
        GetWindowTextW(st->edit, buf, ARRAYSIZE(buf));
        *st->value = buf;
        st->ok = true;
        st->done = true;
        DestroyWindow(h);
        return 0;
      }
      if (LOWORD(w) == IDCANCEL) {
        st->done = true;
        DestroyWindow(h);
        return 0;
      }
      break;
    case WM_CLOSE:
      if (st) st->done = true;
      DestroyWindow(h);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace

bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value) {
  static bool registered = false;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<UINT_PTR>(COLOR_BTNFACE + 1));
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return false;
    registered = true;
  }
  const UINT dpi = GetDpiForSystem();
  auto S = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };

  State st;
  st.value = &value;
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  RECT rc{0, 0, S(440), S(120)};
  AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  RECT area{};
  if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &area);
  else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  const int x = area.left + ((area.right - area.left) - w) / 2, y = area.top + ((area.bottom - area.top) - h) / 2;

  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kClass, title.c_str(), style, x, y, w, h,
                             owner && IsWindowVisible(owner) ? owner : nullptr, nullptr, hi, &st);
  if (!dlg) return false;

  NONCLIENTMETRICSW ncm{sizeof(ncm)};
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
  HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
  auto add = [&](DWORD ex, const wchar_t* cls, const wchar_t* text, DWORD st2, int cx, int cy, int cw, int ch, UINT id) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | st2, S(cx), S(cy), S(cw), S(ch), dlg,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), hi, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
  };
  add(0, L"STATIC", label.c_str(), 0, 12, 10, 416, 20, 0);
  st.edit = add(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 12, 34, 416, 26, 100);
  add(0, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 254, 76, 84, 28, IDOK);
  add(0, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 344, 76, 84, 28, IDCANCEL);
  SendMessageW(dlg, DM_SETDEFID, IDOK, 0);
  SendMessageW(st.edit, EM_SETSEL, 0, -1);

  const bool disableOwner = owner && IsWindowVisible(owner);
  if (disableOwner) EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  SetForegroundWindow(dlg);
  SetFocus(st.edit);

  MSG msg;
  while (!st.done) {
    const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r <= 0) {
      if (r == 0) PostQuitMessage(static_cast<int>(msg.wParam));  // let the app's own loop see WM_QUIT
      if (IsWindow(dlg)) DestroyWindow(dlg);
      break;
    }
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (disableOwner) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
  DeleteObject(font);
  return st.ok;
}

}  // namespace deskskin::ui
