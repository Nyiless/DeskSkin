// WASAPI loopback capture of whatever is playing on the default output device -> FFT bands.
// Fully demand-driven: a worker thread starts on the first Request() and ends by itself
// a few seconds after the last Request(), releasing the audio device. No consumers = no cost.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deskskin::measures {

class AudioCapture {
 public:
  static constexpr int kBands = 64;
  AudioCapture() = default;
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  void Request();                      // call whenever bands are wanted
  void Snapshot(float* out64) const;   // latest bands (0..1)
  unsigned Version() const { return m_version.load(); }
  std::wstring Status() const;         // "" when fine, else a short reason

 private:
  void Run();
  static long long NowMs();

  std::thread m_thread;
  std::atomic<bool> m_running{false}, m_quit{false};
  std::atomic<long long> m_lastRequest{0};
  std::atomic<unsigned> m_version{0};
  mutable std::mutex m_mutex;
  float m_bands[kBands] = {};
  std::wstring m_status;
};

}  // namespace deskskin::measures
