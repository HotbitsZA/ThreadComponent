#pragma once

/**
  simple_container_V2

  Thread-safe adapter around sequence-style STL containers such as
  `std::deque`, `std::list`, and `std::vector`.

  Design goals:
    - move-friendly insert / grab operations
    - no raw-pointer ownership APIs
    - no recursive locking paths
    - explicit, predictable thread-safety contract

  Contract notes:
    - Methods are individually thread-safe.
    - `grab(..., selection_fn, ...)` evaluates the predicate while the
      container lock is held. The predicate must therefore be fast and must
      not call back into the same container.
    - `snapshot(...)` is intended for safe inspection of copyable value types.
*/

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>

template <typename ContainerT, typename = void>
struct simple_container_v2_has_reserve : std::false_type
{
};

template <typename ContainerT>
struct simple_container_v2_has_reserve<
  ContainerT,
  std::void_t<decltype(std::declval<ContainerT&>().reserve(std::declval<std::size_t>()))>>
  : std::true_type
{
};

template <class T, template<typename...> class Container = std::deque>
class simple_container_V2
{
  public:
    enum class enm_Select : std::uint8_t
    {
      skip = 0,
      grab,
      stop,
      stop_and_requeue
    };

    using value_type = T;
    using td_BaseContainer = Container<T>;
    using size_type = typename td_BaseContainer::size_type;
    using fd_SelectionFn = std::function<enm_Select(const value_type&)>;

    simple_container_V2() = default;
    ~simple_container_V2() = default;

    simple_container_V2(const simple_container_V2&) = delete;
    simple_container_V2& operator=(const simple_container_V2&) = delete;
    simple_container_V2(simple_container_V2&&) = delete;
    simple_container_V2& operator=(simple_container_V2&&) = delete;

    void clear()
    {
      std::scoped_lock<std::mutex> lock(m_itemsMutex);
      m_items.clear();
    }

    template <
      typename BaseContainer = td_BaseContainer,
      typename std::enable_if<simple_container_v2_has_reserve<BaseContainer>::value, int>::type = 0>
    void reserve(size_type count)
    {
      std::scoped_lock<std::mutex> lock(m_itemsMutex);
      m_items.reserve(count);
    }

    void insert(value_type item, bool insert_back = true)
    {
      {
        std::scoped_lock<std::mutex> lock(m_itemsMutex);
        insertItemUnlocked(std::move(item), insert_back);
      }

      m_cv.notify_one();
    }

    void insert(td_BaseContainer&& items, bool insert_back = true)
    {
      if (items.empty())
      {
        return;
      }

      {
        std::scoped_lock<std::mutex> lock(m_itemsMutex);
        insertManyUnlocked(std::move(items), insert_back);
      }

      m_cv.notify_one();
    }

    void insert(const td_BaseContainer& items, bool insert_back = true)
    {
      if (items.empty())
      {
        return;
      }

      {
        std::scoped_lock<std::mutex> lock(m_itemsMutex);
        insertManyUnlocked(items, insert_back);
      }

      m_cv.notify_one();
    }

    [[nodiscard]] td_BaseContainer grab(size_type count)
    {
      std::scoped_lock<std::mutex> lock(m_itemsMutex);
      return grabItemsUnlocked(count);
    }

    template <class Rep, class Period>
    [[nodiscard]] td_BaseContainer grab(size_type count, std::chrono::duration<Rep, Period> timeout)
    {
      std::unique_lock<std::mutex> lock(m_itemsMutex);
      m_cv.wait_for(lock, timeout, [this]()
      {
        return !m_items.empty();
      });

      if (m_items.empty())
      {
        return {};
      }

      return grabItemsUnlocked(count);
    }

    [[nodiscard]] td_BaseContainer grab(fd_SelectionFn fn)
    {
      return grab(std::numeric_limits<size_type>::max(), std::move(fn), 0);
    }

    [[nodiscard]] td_BaseContainer grab(size_type count, fd_SelectionFn fn)
    {
      return grab(count, std::move(fn), 0);
    }

    [[nodiscard]] td_BaseContainer grab(fd_SelectionFn fn, size_type offset)
    {
      return grab(std::numeric_limits<size_type>::max(), std::move(fn), offset);
    }

    [[nodiscard]] td_BaseContainer grab(size_type count, fd_SelectionFn fn, size_type offset)
    {
      td_BaseContainer returnContainer;

      if ((count == 0U) || !fn)
      {
        return returnContainer;
      }

      std::scoped_lock<std::mutex> lock(m_itemsMutex);

      auto current = m_items.begin();
      advanceIteratorUnlocked(current, offset);

      while (current != m_items.end())
      {
        switch (fn(*current))
        {
          case enm_Select::stop_and_requeue:
            requeueUnlocked(std::move(returnContainer), true);
            return {};

          case enm_Select::stop:
            return returnContainer;

          case enm_Select::grab:
            returnContainer.insert(returnContainer.end(), std::move(*current));
            current = m_items.erase(current);

            if (returnContainer.size() >= count)
            {
              return returnContainer;
            }
            break;

          case enm_Select::skip:
          default:
            ++current;
            break;
        }
      }

      return returnContainer;
    }

    template <
      typename U = value_type,
      typename std::enable_if<std::is_copy_constructible<U>::value, int>::type = 0>
    [[nodiscard]] td_BaseContainer snapshot(
      size_type count = std::numeric_limits<size_type>::max(),
      size_type offset = 0) const
    {
      td_BaseContainer result;

      std::scoped_lock<std::mutex> lock(m_itemsMutex);

      auto current = m_items.begin();
      advanceIteratorUnlocked(current, offset);

      while ((current != m_items.end()) && (result.size() < count))
      {
        result.insert(result.end(), *current);
        ++current;
      }

      return result;
    }

    void notify_one()
    {
      m_cv.notify_one();
    }

    void notify_all()
    {
      m_cv.notify_all();
    }

    [[nodiscard]] bool empty() const
    {
      std::scoped_lock<std::mutex> lock(m_itemsMutex);
      return m_items.empty();
    }

    [[nodiscard]] size_type size() const
    {
      std::scoped_lock<std::mutex> lock(m_itemsMutex);
      return m_items.size();
    }

  private:
    template <typename IteratorT>
    void advanceIteratorUnlocked(IteratorT& iterator, size_type offset) const
    {
      std::advance(iterator, static_cast<typename std::iterator_traits<IteratorT>::difference_type>(
        std::min(offset, m_items.size())));
    }

    void insertItemUnlocked(value_type&& item, bool insert_back)
    {
      m_items.insert(
        insert_back ? m_items.end() : m_items.begin(),
        std::move(item));
    }

    void insertManyUnlocked(td_BaseContainer&& items, bool insert_back)
    {
      m_items.insert(
        insert_back ? m_items.end() : m_items.begin(),
        std::make_move_iterator(items.begin()),
        std::make_move_iterator(items.end()));
    }

    void insertManyUnlocked(const td_BaseContainer& items, bool insert_back)
    {
      m_items.insert(
        insert_back ? m_items.end() : m_items.begin(),
        items.begin(),
        items.end());
    }

    void requeueUnlocked(td_BaseContainer&& items, bool insert_back)
    {
      if (items.empty())
      {
        return;
      }

      insertManyUnlocked(std::move(items), insert_back);
    }

    [[nodiscard]] td_BaseContainer grabItemsUnlocked(size_type count)
    {
      td_BaseContainer result;
      const auto actualCount = std::min(count, m_items.size());

      auto endIterator = m_items.begin();
      std::advance(
        endIterator,
        static_cast<typename td_BaseContainer::difference_type>(actualCount));

      std::move(m_items.begin(), endIterator, std::back_inserter(result));
      m_items.erase(m_items.begin(), endIterator);

      return result;
    }

    mutable std::mutex m_itemsMutex;
    mutable std::condition_variable m_cv;
    td_BaseContainer m_items;
};
