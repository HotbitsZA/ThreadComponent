#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include "cBaseWorker_V2.h"

class TCPClient_V2 : public cBaseWorker_V2
{
public:
  using Buffer = std::vector<std::uint8_t>;
  using DataCallback = std::function<void(const Buffer &)>;

  TCPClient_V2(
      std::string name,
      std::string serverAddress,
      std::uint16_t port,
      DataCallback receiveCallback = {})
      : cBaseWorker_V2(std::move(name)),
        m_serverAddress(std::move(serverAddress)),
        m_port(port),
        m_receiveCallback(std::move(receiveCallback))
  {
  }

  ~TCPClient_V2() noexcept override
  {
    (void)stopThread();
    closeSocket();
    clearOutboundQueue();
  }

  [[nodiscard]] bool send(Buffer data)
  {
    if (data.empty() || !isRunning())
    {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(m_outboundMutex);
      m_outboundQueue.push(std::move(data));
    }

    m_outboundCondition.notify_one();
    return true;
  }

  [[nodiscard]] bool send(const void *data, std::size_t size)
  {
    if ((data == nullptr) || (size == 0U))
    {
      return false;
    }

    Buffer buffer(size);
    std::memcpy(buffer.data(), data, size);
    return send(std::move(buffer));
  }

  [[nodiscard]] bool send(const std::string &text)
  {
    return send(text.data(), text.size());
  }

protected:
  bool preRun() override
  {
    clearOutboundQueue();

    const int socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0)
    {
      return false;
    }

    m_socket.store(socketFd, std::memory_order_release);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_port);

    if (::inet_pton(AF_INET, m_serverAddress.c_str(), &serverAddr.sin_addr) <= 0)
    {
      closeSocket();
      return false;
    }

    if (::connect(socketFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0)
    {
      closeSocket();
      return false;
    }

    return true;
  }

  void run() override
  {
    std::thread readThread(&TCPClient_V2::readLoop, this);
    std::thread writeThread(&TCPClient_V2::writeLoop, this);

    if (readThread.joinable())
    {
      readThread.join();
    }

    requestStop();
    m_outboundCondition.notify_all();

    if (writeThread.joinable())
    {
      writeThread.join();
    }
  }

  void stopTriggered() override
  {
    closeSocket();
    clearOutboundQueue();
    m_outboundCondition.notify_all();
  }

private:
  static constexpr std::size_t kReadBufferSize = 4096U;

  void readLoop() noexcept
  {
    try
    {
      Buffer buffer(kReadBufferSize);

      while (continueRunning())
      {
        const int socketFd = m_socket.load(std::memory_order_acquire);
        if (socketFd < 0)
        {
          break;
        }

        const auto bytesRead = ::recv(socketFd, buffer.data(), buffer.size(), 0);
        if (bytesRead > 0)
        {
          updateHeartbeat();

          if (m_receiveCallback)
          {
            Buffer payload(
                buffer.begin(),
                buffer.begin() + static_cast<std::size_t>(bytesRead));
            m_receiveCallback(payload);
          }

          continue;
        }

        if ((bytesRead < 0) && (errno == EINTR))
        {
          continue;
        }

        requestStop();
        break;
      }
    }
    catch (...)
    {
      requestStop();
    }
  }

  void writeLoop() noexcept
  {
    try
    {
      while (continueRunning())
      {
        Buffer payload;

        {
          std::unique_lock<std::mutex> lock(m_outboundMutex);
          m_outboundCondition.wait(
              lock,
              [this]()
              {
                return stopRequested() || !m_outboundQueue.empty();
              });

          if (stopRequested())
          {
            break;
          }

          payload = std::move(m_outboundQueue.front());
          m_outboundQueue.pop();
        }

        if (!sendAll(payload))
        {
          requestStop();
          break;
        }

        updateHeartbeat();
      }
    }
    catch (...)
    {
      requestStop();
    }
  }

  [[nodiscard]] bool sendAll(const Buffer &payload) noexcept
  {
    std::size_t totalSent = 0U;

    while (totalSent < payload.size())
    {
      const int socketFd = m_socket.load(std::memory_order_acquire);
      if (socketFd < 0)
      {
        return false;
      }

      const auto bytesSent = ::send(
          socketFd,
          payload.data() + totalSent,
          payload.size() - totalSent,
          0);

      if (bytesSent > 0)
      {
        totalSent += static_cast<std::size_t>(bytesSent);
        continue;
      }

      if ((bytesSent < 0) && (errno == EINTR))
      {
        continue;
      }

      return false;
    }

    return true;
  }

  void clearOutboundQueue() noexcept
  {
    std::lock_guard<std::mutex> lock(m_outboundMutex);
    std::queue<Buffer> emptyQueue;
    m_outboundQueue.swap(emptyQueue);
  }

  void closeSocket() noexcept
  {
    const int socketFd = m_socket.exchange(-1, std::memory_order_acq_rel);
    if (socketFd >= 0)
    {
      ::shutdown(socketFd, SHUT_RDWR);
      ::close(socketFd);
    }
  }

  std::string m_serverAddress;
  std::uint16_t m_port{0U};
  DataCallback m_receiveCallback;

  std::atomic<int> m_socket{-1};

  std::mutex m_outboundMutex;
  std::condition_variable m_outboundCondition;
  std::queue<Buffer> m_outboundQueue;
};
