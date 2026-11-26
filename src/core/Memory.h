// Memory primitives shared by the whole engine.
//  * ObjectPool<T>  - slab free-list; O(1) create/destroy, zero heap churn after warm-up.
//  * FrameScratch   - fixed 16 KiB bump arena (std::pmr) for per-redraw temporaries.
#pragma once
#include <cassert>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

namespace deskskin::core {

template <class T, std::size_t SlabSize = 32>
class ObjectPool {
 public:
  struct Deleter {
    ObjectPool* pool = nullptr;
    void operator()(T* p) const noexcept {
      if (pool) pool->Destroy(p);
    }
  };
  using Ptr = std::unique_ptr<T, Deleter>;

  ObjectPool() = default;
  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;
  ~ObjectPool() { assert(m_live == 0 && "ObjectPool destroyed with live objects (leak)"); }

  template <class... Args>
  Ptr Make(Args&&... args) {
    return Ptr(Create(std::forward<Args>(args)...), Deleter{this});
  }

  template <class... Args>
  T* Create(Args&&... args) {
    void* mem = Allocate();
    try {
      return ::new (mem) T(std::forward<Args>(args)...);
    } catch (...) {
      Free(mem);
      throw;
    }
  }

  void Destroy(T* p) noexcept {
    if (!p) return;
    p->~T();
    Free(p);
  }

  std::size_t LiveCount() const noexcept { return m_live; }

 private:
  union Node {
    Node* next;
    alignas(T) unsigned char storage[sizeof(T)];
  };

  void* Allocate() {
    if (!m_free) Grow();
    Node* n = m_free;
    m_free = n->next;
    ++m_live;
    return n->storage;
  }
  void Free(void* p) noexcept {
    Node* n = reinterpret_cast<Node*>(p);
    n->next = m_free;
    m_free = n;
    --m_live;
  }
  void Grow() {
    auto slab = std::make_unique<Node[]>(SlabSize);
    for (std::size_t i = 0; i < SlabSize; ++i) slab[i].next = (i + 1 < SlabSize) ? &slab[i + 1] : m_free;
    m_free = &slab[0];
    m_slabs.push_back(std::move(slab));
  }

  std::vector<std::unique_ptr<Node[]>> m_slabs;
  Node* m_free = nullptr;
  std::size_t m_live = 0;
};

// Per-redraw scratch memory. Reset() is O(1) and keeps the inline buffer, so steady-state
// redraws never touch the global heap.
class FrameScratch {
 public:
  FrameScratch() : m_res(m_buffer, sizeof(m_buffer), std::pmr::new_delete_resource()) {}
  FrameScratch(const FrameScratch&) = delete;
  FrameScratch& operator=(const FrameScratch&) = delete;

  std::pmr::memory_resource* Resource() noexcept { return &m_res; }
  void Reset() noexcept { m_res.release(); }

 private:
  alignas(std::max_align_t) std::byte m_buffer[16 * 1024];
  std::pmr::monotonic_buffer_resource m_res;
};

}  // namespace deskskin::core
