#include "render/GraphicsDevice.h"


namespace deskskin::render {

using Microsoft::WRL::ComPtr;

HRESULT GraphicsDevice::Initialize() {
  Shutdown();

  static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                              D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required for Direct2D interop

  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels,
                                 ARRAYSIZE(kLevels), D3D11_SDK_VERSION, &m_d3d, nullptr, nullptr);
  if (FAILED(hr)) {  // no usable GPU / RDP session / driver reset: software rasteriser
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels, ARRAYSIZE(kLevels),
                           D3D11_SDK_VERSION, &m_d3d, nullptr, nullptr);
  }
  if (FAILED(hr)) return hr;

  ComPtr<IDXGIDevice> dxgiDevice;
  hr = m_d3d.As(&dxgiDevice);
  if (FAILED(hr)) return hr;

  ComPtr<IDXGIAdapter> adapter;
  hr = dxgiDevice->GetAdapter(&adapter);
  if (FAILED(hr)) return hr;
  hr = adapter->GetParent(IID_PPV_ARGS(&m_dxgiFactory));
  if (FAILED(hr)) return hr;

  D2D1_FACTORY_OPTIONS opts{};
  // Single-threaded: every skin lives on the UI thread, so no locking overhead.
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                         reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
  if (FAILED(hr)) return hr;

  hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
  if (FAILED(hr)) return hr;

  hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
  if (FAILED(hr)) return hr;

  hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcomp));
  if (FAILED(hr)) return hr;

  return S_OK;
}

void GraphicsDevice::Shutdown() noexcept {
  m_dcomp.Reset();
  m_dwrite.Reset();
  m_d2dDevice.Reset();
  m_d2dFactory.Reset();
  m_dxgiFactory.Reset();
  m_d3d.Reset();
}

void GraphicsDevice::Trim() noexcept {
  if (m_d2dDevice) m_d2dDevice->ClearResources(0);
  if (m_d3d) {
    ComPtr<ID3D11DeviceContext> immediate;
    m_d3d->GetImmediateContext(&immediate);
    if (immediate) immediate->ClearState();
    ComPtr<IDXGIDevice3> dxgi3;
    if (SUCCEEDED(m_d3d.As(&dxgi3))) dxgi3->Trim();
  }
  SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

}  // namespace deskskin::render
