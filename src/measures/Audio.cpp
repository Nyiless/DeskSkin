#include "measures/Audio.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "measures/Parse.h"

namespace deskskin::measures {

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::size_t kFft = 1024;
constexpr long long kIdleStopMs = 5000;
}  // namespace

long long AudioCapture::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

AudioCapture::~AudioCapture() {
  m_quit = true;
  if (m_thread.joinable()) m_thread.join();
}

void AudioCapture::Request() {
  m_lastRequest = NowMs();
  if (m_running.load()) return;
  if (m_thread.joinable()) m_thread.join();  // previous run has finished
  m_quit = false;
  m_running = true;
  m_thread = std::thread([this] { Run(); });
}

void AudioCapture::Snapshot(float* out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::memcpy(out, m_bands, sizeof(m_bands));
}

std::wstring AudioCapture::Status() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_status;
}

void AudioCapture::Run() {
  const HRESULT coinit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  auto setStatus = [&](const wchar_t* s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = s;
  };
  {
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDevice> dev;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> cap;
    WAVEFORMATEX* fmt = nullptr;
    bool started = false;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), &en);
    if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&fmt);
    if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr);
    if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), &cap);
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr) || !fmt) {
      setStatus(L"No audio output device available");
    } else {
      started = true;
      setStatus(L"");
      const int channels = std::max<int>(1, fmt->nChannels);
      const bool isFloat = fmt->wBitsPerSample == 32;  // shared-mode mix format is IEEE float32 in practice
      const bool is16 = fmt->wBitsPerSample == 16;
      std::vector<float> ring(kFft * 2, 0.0f), window(kFft), bands(kBands), smoothed(kBands, 0.0f);
      std::size_t write = 0, fresh = 0;
      long long lastDecay = NowMs();

      while (!m_quit.load() && NowMs() - m_lastRequest.load() < kIdleStopMs) {
        Sleep(12);
        UINT32 packet = 0;
        bool gotData = false;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD flags = 0;
          if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
          const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
          for (UINT32 f = 0; f < frames; ++f) {
            float mono = 0.0f;
            if (!silent && data) {
              for (int c = 0; c < channels; ++c) {
                if (isFloat) mono += reinterpret_cast<const float*>(data)[f * channels + c];
                else if (is16) mono += static_cast<float>(reinterpret_cast<const short*>(data)[f * channels + c]) / 32768.0f;
              }
              mono /= static_cast<float>(channels);
            }
            ring[write] = mono;
            write = (write + 1) % ring.size();
            ++fresh;
          }
          cap->ReleaseBuffer(frames);
          gotData = true;
        }

        const long long now = NowMs();
        if (gotData && fresh >= kFft / 2) {
          fresh = 0;
          for (std::size_t i = 0; i < kFft; ++i) window[i] = ring[(write + ring.size() - kFft + i) % ring.size()];
          parse::BandsFromSamples(window.data(), kFft, static_cast<float>(fmt->nSamplesPerSec), kBands, bands.data());
          for (int b = 0; b < kBands; ++b) smoothed[b] = std::max(bands[b], smoothed[b] * 0.82f);  // fast attack, smooth decay
          lastDecay = now;
        } else if (now - lastDecay > 33) {  // loopback delivers nothing while silent: fade out
          bool any = false;
          for (int b = 0; b < kBands; ++b) { smoothed[b] *= 0.85f; if (smoothed[b] < 0.01f) smoothed[b] = 0.0f; any = any || smoothed[b] > 0.0f; }
          lastDecay = now;
          if (!any) continue;
        } else {
          continue;
        }
        {
          std::lock_guard<std::mutex> lock(m_mutex);
          std::memcpy(m_bands, smoothed.data(), sizeof(m_bands));
        }
        ++m_version;
      }
    }
    if (started) client->Stop();
    if (fmt) CoTaskMemFree(fmt);
  }
  if (SUCCEEDED(coinit)) CoUninitialize();
  m_running = false;
}

}  // namespace deskskin::measures
