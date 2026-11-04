#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Lighthouse::Memory
{
[[nodiscard]] void* Allocate(std::size_t size);
void Free(void* ptr) noexcept;
[[nodiscard]] void* AllocateAligned(std::size_t size, std::size_t alignment);
void FreeAligned(void* ptr) noexcept;

template <typename T> class Allocator
{
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  using is_always_equal = std::true_type;

  constexpr Allocator() noexcept = default;
  template <typename U> constexpr Allocator(const Allocator<U>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t count)
  {
    if (count > max_size())
      throw std::bad_array_new_length();
    return static_cast<T*>(AllocateAligned(count * sizeof(T), alignof(T)));
  }

  void deallocate(T* ptr, std::size_t) noexcept
  {
    FreeAligned(ptr);
  }

  [[nodiscard]] constexpr std::size_t max_size() const noexcept
  {
    return std::numeric_limits<std::size_t>::max() / sizeof(T);
  }

  template <typename U> struct rebind
  {
    using other = Allocator<U>;
  };
};

template <typename T, typename U>
[[nodiscard]] constexpr bool operator==(const Allocator<T>&,
                                        const Allocator<U>&) noexcept
{
  return true;
}

template <typename T, typename U>
[[nodiscard]] constexpr bool operator!=(const Allocator<T>&,
                                        const Allocator<U>&) noexcept
{
  return false;
}

template <typename T, typename... Args> [[nodiscard]] T* New(Args&&... args)
{
  void* storage = AllocateAligned(sizeof(T), alignof(T));
  try
  {
    return std::construct_at(static_cast<T*>(storage),
                             std::forward<Args>(args)...);
  }
  catch (...)
  {
    FreeAligned(storage);
    throw;
  }
}

template <typename T>
void Delete(T* ptr) noexcept(std::is_nothrow_destructible_v<T>)
{
  if (ptr == nullptr)
    return;
  std::destroy_at(ptr);
  FreeAligned(ptr);
}
template <typename T> struct Deleter
{
  void operator()(T* ptr) const noexcept(std::is_nothrow_destructible_v<T>)
  {
    Delete(ptr);
  }
};
} // namespace AtlasNet::Memory
