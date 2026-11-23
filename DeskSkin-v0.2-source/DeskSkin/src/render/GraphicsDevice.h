// ONE set of GPU objects for the whole process. Every skin window only adds a swap chain and a
// device context on top of this, which is what keeps the per-skin cost small.
#pragma once
#include "platform/DesktopHost.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace deskskin::render {

class GraphicsDevice {
 public:
  GraphicsDevice() = default;
  GraphicsDevice(const GraphicsDevice&) = delete;
  GraphicsDevice& operator=(const GraphicsDevice&) = delete;

  // Safe to call again after device loss (releases everything first).
  HRESULT Initialize();
  void Shutdown() noexcept;

  // Hand cached GPU/driver memory back to the OS and trim the working set. Call after load and
  // whenever the engine goes idle.
  void Trim() noexcept;

  ID3D11Device* D3D() const noexcept { return m_d3d.Get(); }
  IDXGIFactory2* DxgiFactory() const noexcept { return m_dxgiFactory.Get(); }
  ID2D1Factory1* D2DFactory() const noexcept { return m_d2dFactory.Get(); }
  ID2D1Device* D2DDevice() const noexcept { return m_d2dDevice.Get(); }
  IDWriteFactory* DWrite() const noexcept { return m_dwrite.Get(); }
  IDCompositionDevice* Dcomp() const noexcept { return m_dcomp.Get(); }

 private:
  Microsoft::WRL::ComPtr<ID3D11Device> m_d3d;
  Microsoft::WRL::ComPtr<IDXGIFactory2> m_dxgiFactory;
  Microsoft::WRL::ComPtr<ID2D1Factory1> m_d2dFactory;
  Microsoft::WRL::ComPtr<ID2D1Device> m_d2dDevice;
  Microsoft::WRL::ComPtr<IDWriteFactory> m_dwrite;
  Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcomp;
};

}  // namespace deskskin::render
