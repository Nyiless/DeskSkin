# DeskSkin: architecture (v0.1 foundation)

"DeskSkin" is a placeholder name. Native C++20, Win32, Direct2D/DirectWrite/DirectComposition. No web runtime, no .NET.

## 1. Project layout

```
DeskSkin/
├── CMakeLists.txt
├── docs/ARCHITECTURE.md
├── skins/Demo/Demo.ini               shipped sample skin
├── tests/ini_tests.cpp               portable tests (run with ASan/UBSan)
└── src/
    ├── main.cpp                      WinMain, hidden control window, tray, skin discovery, timer,
    │                                 Explorer-restart + GPU-loss recovery
    ├── core/
    │   └── Memory.h                  ObjectPool<T>, FrameScratch (std::pmr arena)
    ├── config/
    │   ├── IniParser.h/.cpp          single-blob .ini parser, #Var# expansion, UTF-8/16 input
    ├── platform/
    │   └── DesktopHost.h/.cpp        Progman/WorkerW discovery (legacy + 24H2 layouts)
    ├── render/
    │   ├── GraphicsDevice.h/.cpp     ONE shared D3D11 + D2D + DWrite + DComp device
    │   └── SkinWindow.h/.cpp         transparent child window, swap chain, dirty-rect renderer
    └── skin/
        └── BasicSkin.h/.cpp          .ini -> String/Bar meters (ISkinContent implementation)
```

Planned (not in v0.1), each behind an interface so the core never depends on them:

```
src/measures/    IMeasure + CpuMeasure (PDH), RamMeasure, NetMeasure, GpuMeasure (DXGI/PDH),
                 AudioLoopback (WASAPI) - one shared polling scheduler, results pushed to skins
src/script/      Lua 5.4 host: custom allocator with hard cap, stripped stdlib, instruction budget
src/render/      TextureAtlas (shared image meters), HistogramMeter, ImageMeter
src/packages/    .dskin (zip) install/uninstall, manifest + signature check
```

## 2. Memory-management strategy

Goals: no leaks by construction, no steady-state heap churn, per-skin cost dominated by pixels.

| Layer | Technique | Where |
|---|---|---|
| Ownership | RAII everywhere: `unique_ptr`, `ComPtr`, no naked `new`/`delete`, no manual `Release()`. Destruction order is encoded in member/declaration order (window before content, content before pool, skins before GPU device). | all |
| Shared GPU state | One `GraphicsDevice` (D3D11 device, D2D device, DWrite factory, DComp device) for the whole process. A skin adds only: swap chain + `ID2D1DeviceContext` + DComp visual. | `GraphicsDevice` |
| Pixels | `WS_EX_NOREDIRECTIONBITMAP`: no GDI backing store. Two flip-model buffers per skin; `300x130 @100%` is about 0.3 MB of GPU memory. | `SkinWindow` |
| Event-driven redraw | Nothing renders unless `Invalidate()` is called. Message loop blocks in `GetMessage`. Timer exists only if some skin has time-varying text, at the fastest requested rate. Unchanged values do not invalidate (`BasicSkin::Tick` compares text first). | `main`, `BasicSkin` |
| Dirty rectangles | Invalidations union into one rect; `WM_PAINT` coalescing is free from the OS; the frame clips to that rect, clears only it, and passes it to `Present1` as a dirty rect. Because of the 2-buffer flip chain, each frame repaints `dirty(now) U dirty(previous)`. Meters outside the rect are culled. | `SkinWindow`, `BasicSkin` |
| Object pooling | `ObjectPool<T>`: slab free-list, O(1) create/destroy, slots recycled on skin reload; debug assert on leaked objects at pool destruction. All `Meter`s of all skins come from one pool. | `core/Memory.h` |
| Scratch memory | `FrameScratch`: fixed 16 KiB inline `std::pmr::monotonic_buffer_resource`, `Reset()` per redraw. Hot paths (layout, formatting) allocate from it instead of the heap. | `core/Memory.h` |
| Config memory | Parsed `.ini` = one contiguous `vector<char>` blob + 20-byte entry records (offsets, not pointers). Parser temporaries die before the skin finishes loading. | `IniParser` |
| Idle trim | After load/unload, `GraphicsDevice::Trim()` runs `ID2D1Device::ClearResources`, `IDXGIDevice3::Trim`, `ClearState` and trims the working set. | `GraphicsDevice` |
| Build | Static CRT (no vcruntime DLLs), `/O1`, LTCG, `/OPT:REF /OPT:ICF`, function-level linking. | `CMakeLists.txt` |
| Planned | Texture atlas for image meters (one `ID2D1Bitmap1` per atlas, sub-rect draws). Lua: `lua_newstate` with a counting allocator that refuses past a cap (e.g. 512 KiB/skin), no `io`/`os`/`package`/`debug`, `lua_sethook` instruction budget. | roadmap |

### Realistic RAM budget (read this before trusting the 15 MB target)

I have **not measured** this build (no Windows toolchain was available while writing it), so treat these as engineering expectations, not results.

- The Direct3D 11 + DXGI + DirectComposition stack loads GPU user-mode driver DLLs. Depending on vendor, that alone can use a large part of, or exceed, 15 MB of working set before any skin exists. Much of it is shared/file-backed, so measure **private working set** (Task Manager "Memory (active private working set)", or `PROCESS_MEMORY_COUNTERS_EX2::PrivateWorkingSetSize`) rather than plain "Working Set".
- If the GPU path misses the budget, the planned fallback is a second backend: `ID2D1DCRenderTarget` over a 32-bit premultiplied DIB section, presented with `UpdateLayeredWindow`. It never loads a GPU driver, keeps one bitmap per skin, and is entirely adequate for 1 Hz text and bar meters. `SkinWindow` is the only class that would need a sibling implementation; `ISkinContent` and everything above it stay unchanged. Do this early if measurements demand it. It is easier before meters multiply.
- Things that cost memory and are deliberately avoided: per-skin D3D devices, per-skin DWrite factories, per-frame heap allocation, GDI+ , any CRT DLLs, large swap chains (a skin should be sized to its content).

## 3. Rendering pipeline

```
Measure/Timer -> BasicSkin::Tick -> SkinWindow::Invalidate(dirtyDIPs)
              -> InvalidateRect (OS coalesces) -> WM_PAINT -> SkinWindow::Render
                    GetBuffer(0) -> D2D target bitmap -> BeginDraw
                    PushAxisAlignedClip(dirty U prevDirty) -> Clear(transparent)
                    ISkinContent::Draw(ctx, clip)           // only meters intersecting clip
                    EndDraw -> Present1(dirtyRects)         // DWM composites premultiplied alpha
```

Why DirectComposition + a composition swap chain rather than `UpdateLayeredWindow`: the GPU does the compositing, per-pixel alpha is native, and `Present1` dirty rects tell DWM exactly what changed.

Text on transparent surfaces uses grayscale anti-aliasing (`D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE`); ClearType requires an opaque background and fringes otherwise.

## 4. Desktop integration

- Send `0x052C` to `Progman` so it creates the wallpaper `WorkerW`.
- Windows 10 / 11 <= 23H2: `WorkerW` is a top-level sibling after the window that hosts `SHELLDLL_DefView`.
- Windows 11 24H2+: `WorkerW` is a **child of Progman**, below `SHELLDLL_DefView`. `DesktopHost::Attach` tries this first.
- Skins are `WS_CHILD` windows of that `WorkerW`: behind icons, never hidden by Win+D because they are not top-level windows.
- Explorer restart destroys the parent and therefore every skin window. The control window listens for the `TaskbarCreated` broadcast, re-attaches, and calls `SkinWindow::Recreate()`.
- GPU device loss: `SkinWindow` posts `WM_DEVICE_LOST`; `App::Reload(true)` rebuilds the device and all skins.

## 5. Known limitations / next steps in priority order

1. **Mouse input (v0.2: implemented, unverified on real hardware).** Windows behind the icon layer get no mouse messages, so `input/MouseRouter` installs a `WH_MOUSE_LL` hook. The hook only reacts to clicks and to moves while a press started on a skin; it never swallows moves, and anything slow (menus, ShellExecute, file writes) is posted to the control window. A click on a skin wins over a desktop icon underneath it. `IsDesktopSurface` (class of `WindowFromPoint`'s root is `Progman`/`WorkerW`) keeps real windows and the taskbar fully functional.
2. **Measures.** PDH for CPU/RAM/network, DXGI/PDH GPU counters (GPU temperature is vendor-specific: NVML/ADL or WMI, no universal Win32 API), WASAPI loopback on a worker thread feeding a lock-free ring buffer. One scheduler thread, results marshalled to the UI thread by `PostMessage`.
3. **Lua.** Standard Lua 5.4, custom allocator with a hard cap, whitelisted libraries, instruction-count hook, one `lua_State` per skin.
4. **Meters.** Image (atlas) and Histogram (ring buffer + `ID2D1PathGeometry` or line list).
5. **Skin manager.** Live `.ini` reload via `ReadDirectoryChangesW`, `.dskin` package install, per-skin position persistence, drag-to-move (needs item 1), per-monitor DPI changes (child windows do not get `WM_DPICHANGED`; observe `WM_DISPLAYCHANGE` on the control window and `Resize`).
6. Multi-rect dirty regions (currently one bounding rect per frame) if skins grow large with scattered updates.
7. Coalesce multiple `WM_DEVICE_LOST` posts into one reload.

## 5b. v0.2 additions

`config/Settings` (atomic `settings.ini`: position, enabled, locked, click-through per skin), `platform/Autostart` (HKCU Run), skin change notification (`FindFirstChangeNotification`, debounced reload, no polling), single-instance mutex, startup retry until Explorer's desktop exists, embedded default skin written on first run, `DeskSkin.log`, icon/manifest/version resource, and a top-level-window fallback in `SkinWindow::Create` if composition on the wallpaper-layer child is refused.

**Biggest unverified risk:** DirectComposition + `WS_EX_NOREDIRECTIONBITMAP` on a *child* of `WorkerW`, especially on Windows 11 24H2 where the shell layout changed. `DeskSkin.log` records the layout found and whether each skin ended up on the wallpaper layer (`onWallpaperLayer=true/false`). If that proves unreliable, the planned alternative is a layered-window (`UpdateLayeredWindow`) backend for `SkinWindow`.

## 5c. v0.4: Widget Library and live data

* `measures/Hub` serves every live value (`%CPU%`, `%DISK:C%`, `%NET_DOWN%`, `%WEATHER_TEMP%`, `%MEDIA_TITLE%`, `%FEED_1%`...)
  lazily: a measure is sampled only when a skin asks and at most once per its interval. Weather/feeds fetch on short-lived
  worker threads (WinHTTP) and degrade to readable text when offline. Audio (WASAPI loopback + FFT, `measures/Audio`) starts on
  first request and stops itself 5 s after the last one. Now-playing reads the Spotify desktop window title; transport buttons send
  media keys, so no credentials are involved anywhere.
* `skin/BasicSkin` gained Histogram, Bars (audio), Hex and Calendar meters, `Source=` data binding, `Align=`, extra time tokens and
  click actions (`Open:`, `Run:`, `Cmd:`).
* `library/` is generated from `tools/library_spec.py` (skin text is compiled in). Options become ini variable overrides
  (`ParseOptions::overrides`); `shared.*` options (city, units, feed URL) feed the Hub instead.
* `ui/DashboardLibrary.cpp` is the Library page; previews are the real skins drawn by the real engine into the dashboard.
* `platform/TaskbarEffect` (experimental) uses the undocumented `SetWindowCompositionAttribute`.
* Portable tests: ini, settings, parse (JSON/RSS/FFT), catalog (every generated skin is validated).

## 6. Building

```
build.bat                          # or: cmake -S . -B build -A x64 && cmake --build build --config Release
# No local tools? Push to GitHub: .github/workflows/build.yml builds, tests and uploads dist/DeskSkin as an artifact.
```

Portable parser tests (any OS): `g++ -std=c++20 -Isrc tests/ini_tests.cpp src/config/IniParser.cpp && ./a.out`. Requires Windows 10 1703+ (per-monitor v2 DPI) and the Windows SDK.
