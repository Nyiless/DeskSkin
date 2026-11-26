#include "platform/TaskbarEffect.h"

namespace deskskin::platform {
namespace {

struct AccentPolicy {
  int state;
  int flags;
  unsigned gradientColor;  // 0xAABBGGRR
  int animationId;
};
struct CompositionAttrib {
  int attribute;
  void* data;
  unsigned long size;
};
using SetCompositionFn = BOOL(WINAPI*)(HWND, CompositionAttrib*);

SetCompositionFn Fn() {
  static SetCompositionFn fn = reinterpret_cast<SetCompositionFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
  return fn;
}

constexpr int kAccentDisabled = 0, kAccentTransparentGradient = 2, kAccentBlur = 3, kAccentAcrylic = 4;
constexpr int kWcaAccentPolicy = 19;

void SetAccent(HWND h, int state, unsigned color) {
  if (auto fn = Fn()) {
    AccentPolicy p{state, 2, color, 0};
    CompositionAttrib a{kWcaAccentPolicy, &p, sizeof(p)};
    fn(h, &a);
  }
}

template <class F>
void ForEachTaskbar(F&& f) {
  if (HWND h = FindWindowW(L"Shell_TrayWnd", nullptr)) f(h);
  for (HWND h = nullptr; (h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr;) f(h);
}

}  // namespace

void TaskbarEffect::Apply(TaskbarStyle style, unsigned tintAlpha) {
  const unsigned color = (tintAlpha & 0xFFu) << 24 | 0x00101010u;
  const int state = style == TaskbarStyle::Clear ? kAccentTransparentGradient
                    : style == TaskbarStyle::Blur ? kAccentBlur : kAccentAcrylic;
  ForEachTaskbar([&](HWND h) { SetAccent(h, state, style == TaskbarStyle::Clear ? (tintAlpha & 0xFFu) << 24 : color); });
}

void TaskbarEffect::Restore() {
  ForEachTaskbar([](HWND h) { SetAccent(h, kAccentDisabled, 0); });
}

}  // namespace deskskin::platform
