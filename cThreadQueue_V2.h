#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <typename Type>
class cThreadQueue_V2
{
public:
    cThreadQueue_V2() noexcept = default;
    cThreadQueue_V2(const cThreadQueue_V2 &) = delete;
    cThreadQueue_V2 &operator=(const cThreadQueue_V2 &) = delete;

    cThreadQueue_V2(cThreadQueue_V2 &&other) noexcept
    {
        moveFrom(std::move(other));
    }

    cThreadQueue_V2 &operator=(cThreadQueue_V2 &&other) noexcept
    {
        if (this != &other)
            moveFrom(std::move(other));

        return *this;
    }

    cThreadQueue_V2 &operator=(std::queue<Type> &&queue)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue = std::move(queue);
        syncCachedSizeLocked();
        return *this;
    }

    ~cThreadQueue_V2() = default;

    void clear() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue = std::queue<Type>{};
        m_queueSize.store(0u, std::memory_order_relaxed);
    }

    template <typename ValueType = Type>
    void push(ValueType &&element)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::forward<ValueType>(element));
        syncCachedSizeLocked();
    }

    void push(std::queue<Type> elements)
    {
        if (elements.empty())
            return;

        std::lock_guard<std::mutex> lock(m_mutex);
        while (!elements.empty())
        {
            m_queue.push(std::move(elements.front()));
            elements.pop();
        }
        syncCachedSizeLocked();
    }

    template <typename... Args>
    void emplace(Args &&...args)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.emplace(std::forward<Args>(args)...);
        syncCachedSizeLocked();
    }

    [[nodiscard]] std::optional<Type> tryGetFront()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty())
            return std::nullopt;

        Type element = std::move(m_queue.front());
        m_queue.pop();
        syncCachedSizeLocked();
        return std::optional<Type>(std::move(element));
    }

    bool tryGetFront(Type &element)
    {
        auto popped = tryGetFront();
        if (!popped.has_value())
            return false;

        element = std::move(*popped);
        return true;
    }

    [[nodiscard]] Type getFront()
    {
        auto popped = tryGetFront();
        if (!popped.has_value())
            throw std::underflow_error("cThreadQueue_V2::getFront called on an empty queue");

        return std::move(*popped);
    }

    [[nodiscard]] std::queue<Type> getFront(std::size_t elementCount)
    {
        std::queue<Type> elements;
        if (elementCount == 0u)
            return elements;

        std::lock_guard<std::mutex> lock(m_mutex);
        while ((elementCount > 0u) && !m_queue.empty())
        {
            elements.push(std::move(m_queue.front()));
            m_queue.pop();
            --elementCount;
        }

        syncCachedSizeLocked();
        return elements;
    }

    [[nodiscard]] std::queue<Type> getAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<Type> elements = std::move(m_queue);
        m_queue = std::queue<Type>{};
        m_queueSize.store(0u, std::memory_order_relaxed);
        return elements;
    }

    [[nodiscard]] bool empty(bool useCached = true) const noexcept
    {
        return size(useCached) == 0u;
    }

    [[nodiscard]] std::size_t size(bool useCached = true) const noexcept
    {
        if (useCached)
            return m_queueSize.load(std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    void moveFrom(cThreadQueue_V2 &&other) noexcept
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_queue = std::move(other.m_queue);
        syncCachedSizeLocked();
        other.m_queue = std::queue<Type>{};
        other.m_queueSize.store(0u, std::memory_order_relaxed);
    }

    void syncCachedSizeLocked() noexcept
    {
        m_queueSize.store(m_queue.size(), std::memory_order_relaxed);
    }

    mutable std::mutex m_mutex;
    std::queue<Type> m_queue;
    std::atomic<std::size_t> m_queueSize{0u};
};
