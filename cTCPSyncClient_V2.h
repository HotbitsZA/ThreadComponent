#pragma once

/**
  cTCPSyncClient_V2

  Synchronous RAII TCP client for request/response style communication.

  Design goals:
    - no background worker threads
    - no raw ownership / no new / delete
    - explicit connect / send / receive / close flow
    - convenient one-shot exchange helper for short-lived requests

  Notes:
    - This class is intentionally not thread-safe.
    - TCP is a byte stream, so the caller still needs a framing strategy.
      Use `receiveExact`, `receiveUntilDelimiter`, or `receiveUntilClosed`
      depending on the protocol.
*/

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "scope_exit.h"

class cTCPSyncClient_V2
{
public:
  using Buffer = std::vector<std::uint8_t>;
  using duration_type = std::chrono::milliseconds;

  enum class enm_IOResult : std::uint8_t
  {
    Success = 0,
    Closed,
    Timeout,
    NotConnected,
    InvalidArgument,
    Failed
  };

  static constexpr std::size_t kDefaultReceiveChunkSize = 4096U;
  static constexpr std::size_t kDefaultMaxPayloadSize = 64U * 1024U;

  cTCPSyncClient_V2() = delete;
  cTCPSyncClient_V2(const cTCPSyncClient_V2 &) = delete;
  cTCPSyncClient_V2 &operator=(const cTCPSyncClient_V2 &) = delete;
  cTCPSyncClient_V2(cTCPSyncClient_V2 &&) = delete;
  cTCPSyncClient_V2 &operator=(cTCPSyncClient_V2 &&) = delete;

  cTCPSyncClient_V2(std::string serverAddress, std::uint16_t port)
      : m_serverAddress(std::move(serverAddress)),
        m_port(port)
  {
  }

  ~cTCPSyncClient_V2() noexcept
  {
    close();
  }

  [[nodiscard]] const std::string &serverAddress() const noexcept
  {
    return m_serverAddress;
  }

  [[nodiscard]] std::uint16_t port() const noexcept
  {
    return m_port;
  }

  [[nodiscard]] bool isConnected() const noexcept
  {
    return m_socket >= 0;
  }

  [[nodiscard]] duration_type sendTimeout() const noexcept
  {
    return m_sendTimeout;
  }

  [[nodiscard]] duration_type receiveTimeout() const noexcept
  {
    return m_receiveTimeout;
  }

  [[nodiscard]] bool setSendTimeout(duration_type timeout) noexcept
  {
    m_sendTimeout = sanitizeTimeout(timeout);
    return applyConfiguredTimeout(m_sendTimeout, SO_SNDTIMEO);
  }

  [[nodiscard]] bool setReceiveTimeout(duration_type timeout) noexcept
  {
    m_receiveTimeout = sanitizeTimeout(timeout);
    return applyConfiguredTimeout(m_receiveTimeout, SO_RCVTIMEO);
  }

  [[nodiscard]] bool connect() noexcept
  {
    close();
    clearLastError();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *rawResults = nullptr;
    const auto portText = std::to_string(m_port);
    const int addressInfoResult =
        ::getaddrinfo(m_serverAddress.c_str(), portText.c_str(), &hints, &rawResults);

    if (addressInfoResult != 0)
    {
      setLastAddressInfoError(addressInfoResult);
      return false;
    }

    using AddrInfoPtr = std::unique_ptr<addrinfo, void (*)(addrinfo *)>;
    AddrInfoPtr addressResults(rawResults, ::freeaddrinfo);

    for (addrinfo *current = addressResults.get(); current != nullptr; current = current->ai_next)
    {
      const int socketFd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
      if (socketFd < 0)
      {
        setLastPlatformError(errno);
        continue;
      }

      if (!configureSocket(socketFd))
      {
        closeSocketValue(socketFd);
        continue;
      }

      if (::connect(socketFd, current->ai_addr, current->ai_addrlen) == 0)
      {
        m_socket = socketFd;
        clearLastError();
        return true;
      }

      setLastPlatformError(errno);
      closeSocketValue(socketFd);
    }

    return false;
  }

  [[nodiscard]] bool reconnect() noexcept
  {
    close();
    return connect();
  }

  void close() noexcept
  {
    if (m_socket >= 0)
    {
      closeSocketValue(m_socket);
      m_socket = -1;
    }
  }

  [[nodiscard]] bool shutdownWrite() noexcept
  {
    if (!isConnected())
    {
      return false;
    }

    clearLastError();

    if (::shutdown(m_socket, SHUT_WR) < 0)
    {
      setLastPlatformError(errno);
      return false;
    }

    return true;
  }

  [[nodiscard]] enm_IOResult sendAll(const Buffer &payload) noexcept
  {
    return sendAll(payload.data(), payload.size());
  }

  [[nodiscard]] enm_IOResult sendAll(std::string_view payload) noexcept
  {
    return sendAll(payload.data(), payload.size());
  }

  [[nodiscard]] enm_IOResult sendAll(const void *data, std::size_t size) noexcept
  {
    if (size == 0U)
    {
      clearLastError();
      return enm_IOResult::Success;
    }

    if (data == nullptr)
    {
      clearLastError();
      return enm_IOResult::InvalidArgument;
    }

    if (!isConnected())
    {
      clearLastError();
      return enm_IOResult::NotConnected;
    }

    clearLastError();

    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::size_t totalSent = 0U;

    while (totalSent < size)
    {
      const auto bytesSent = ::send(
          m_socket,
          bytes + totalSent,
          size - totalSent,
          platformSendFlags());

      if (bytesSent > 0)
      {
        totalSent += static_cast<std::size_t>(bytesSent);
        continue;
      }

      if ((bytesSent < 0) && (errno == EINTR))
      {
        continue;
      }

      return classifySocketError(errno);
    }

    return enm_IOResult::Success;
  }

  [[nodiscard]] enm_IOResult receiveSome(
      Buffer &out,
      std::size_t maxBytes = kDefaultReceiveChunkSize) noexcept
  {
    out.clear();

    if (maxBytes == 0U)
    {
      clearLastError();
      return enm_IOResult::InvalidArgument;
    }

    if (!isConnected())
    {
      clearLastError();
      return enm_IOResult::NotConnected;
    }

    clearLastError();

    Buffer buffer(maxBytes);
    while (true)
    {
      const auto bytesRead = ::recv(m_socket, buffer.data(), buffer.size(), 0);
      if (bytesRead > 0)
      {
        buffer.resize(static_cast<std::size_t>(bytesRead));
        out = std::move(buffer);
        return enm_IOResult::Success;
      }

      if (bytesRead == 0)
      {
        return enm_IOResult::Closed;
      }

      if (errno == EINTR)
      {
        continue;
      }

      return classifySocketError(errno);
    }
  }

  [[nodiscard]] enm_IOResult receiveExact(std::size_t bytesToRead, Buffer &out) noexcept
  {
    out.clear();

    if (bytesToRead == 0U)
    {
      clearLastError();
      return enm_IOResult::Success;
    }

    if (!isConnected())
    {
      clearLastError();
      return enm_IOResult::NotConnected;
    }

    clearLastError();

    out.resize(bytesToRead);
    std::size_t totalRead = 0U;

    while (totalRead < bytesToRead)
    {
      const auto bytesRead = ::recv(m_socket, out.data() + totalRead, bytesToRead - totalRead, 0);
      if (bytesRead > 0)
      {
        totalRead += static_cast<std::size_t>(bytesRead);
        continue;
      }

      if (bytesRead == 0)
      {
        out.resize(totalRead);
        return enm_IOResult::Closed;
      }

      if (errno == EINTR)
      {
        continue;
      }

      out.resize(totalRead);
      return classifySocketError(errno);
    }

    return enm_IOResult::Success;
  }

  [[nodiscard]] enm_IOResult receiveUntilDelimiter(
      std::string_view delimiter,
      Buffer &out,
      std::size_t maxBytes = kDefaultMaxPayloadSize) noexcept
  {
    if (delimiter.empty() || (maxBytes == 0U))
    {
      out.clear();
      clearLastError();
      return enm_IOResult::InvalidArgument;
    }

    Buffer delimiterBuffer(delimiter.begin(), delimiter.end());
    return receiveUntilDelimiter(delimiterBuffer, out, maxBytes);
  }

  [[nodiscard]] enm_IOResult receiveUntilDelimiter(
      const Buffer &delimiter,
      Buffer &out,
      std::size_t maxBytes = kDefaultMaxPayloadSize) noexcept
  {
    out.clear();

    if (delimiter.empty() || (maxBytes == 0U))
    {
      clearLastError();
      return enm_IOResult::InvalidArgument;
    }

    if (!isConnected())
    {
      clearLastError();
      return enm_IOResult::NotConnected;
    }

    clearLastError();

    Buffer chunk;
    while (out.size() < maxBytes)
    {
      const auto nextChunkSize = std::min(kDefaultReceiveChunkSize, maxBytes - out.size());
      const auto readResult = receiveSome(chunk, nextChunkSize);
      if (readResult == enm_IOResult::Success)
      {
        out.insert(out.end(), chunk.begin(), chunk.end());

        if (std::search(out.begin(), out.end(), delimiter.begin(), delimiter.end()) != out.end())
        {
          return enm_IOResult::Success;
        }

        continue;
      }

      return readResult;
    }

    setLastPlatformError(EMSGSIZE);
    return enm_IOResult::Failed;
  }

  [[nodiscard]] enm_IOResult receiveUntilClosed(
      Buffer &out,
      std::size_t maxBytes = kDefaultMaxPayloadSize) noexcept
  {
    out.clear();

    if (maxBytes == 0U)
    {
      clearLastError();
      return enm_IOResult::InvalidArgument;
    }

    if (!isConnected())
    {
      clearLastError();
      return enm_IOResult::NotConnected;
    }

    clearLastError();

    Buffer chunk;
    while (out.size() < maxBytes)
    {
      const auto nextChunkSize = std::min(kDefaultReceiveChunkSize, maxBytes - out.size());
      const auto readResult = receiveSome(chunk, nextChunkSize);
      if (readResult == enm_IOResult::Success)
      {
        out.insert(out.end(), chunk.begin(), chunk.end());
        continue;
      }

      if (readResult == enm_IOResult::Closed)
      {
        clearLastError();
        return enm_IOResult::Success;
      }

      return readResult;
    }

    setLastPlatformError(EMSGSIZE);
    return enm_IOResult::Failed;
  }

  [[nodiscard]] enm_IOResult exchangeOnce(
      std::string_view request,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultReceiveChunkSize) noexcept
  {
    return exchangeOnce(request.data(), request.size(), response, maxResponseBytes);
  }

  [[nodiscard]] enm_IOResult exchangeOnce(
      const Buffer &request,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultReceiveChunkSize) noexcept
  {
    return exchangeOnce(request.data(), request.size(), response, maxResponseBytes);
  }

  [[nodiscard]] enm_IOResult exchangeOnce(
      const void *requestData,
      std::size_t requestSize,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultReceiveChunkSize) noexcept
  {
    if (!connect())
    {
      response.clear();
      return enm_IOResult::Failed;
    }

    auto closeOnExit = hotbits::MakeScopeExit([this]() noexcept
                                              { close(); });

    const auto sendResult = sendAll(requestData, requestSize);
    if (sendResult != enm_IOResult::Success)
    {
      response.clear();
      return sendResult;
    }

    return receiveSome(response, maxResponseBytes);
  }

  [[nodiscard]] enm_IOResult exchangeOnceUntilClosed(
      std::string_view request,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultMaxPayloadSize) noexcept
  {
    return exchangeOnceUntilClosed(request.data(), request.size(), response, maxResponseBytes);
  }

  [[nodiscard]] enm_IOResult exchangeOnceUntilClosed(
      const Buffer &request,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultMaxPayloadSize) noexcept
  {
    return exchangeOnceUntilClosed(request.data(), request.size(), response, maxResponseBytes);
  }

  [[nodiscard]] enm_IOResult exchangeOnceUntilClosed(
      const void *requestData,
      std::size_t requestSize,
      Buffer &response,
      std::size_t maxResponseBytes = kDefaultMaxPayloadSize) noexcept
  {
    if (!connect())
    {
      response.clear();
      return enm_IOResult::Failed;
    }

    auto closeOnExit = hotbits::MakeScopeExit([this]() noexcept
                                              { close(); });

    const auto sendResult = sendAll(requestData, requestSize);
    if (sendResult != enm_IOResult::Success)
    {
      response.clear();
      return sendResult;
    }

    if (!shutdownWrite())
    {
      response.clear();
      return enm_IOResult::Failed;
    }

    return receiveUntilClosed(response, maxResponseBytes);
  }

  [[nodiscard]] int lastPlatformError() const noexcept
  {
    return m_lastPlatformError;
  }

  [[nodiscard]] int lastAddressInfoError() const noexcept
  {
    return m_lastAddressInfoError;
  }

  [[nodiscard]] std::string lastErrorMessage() const
  {
    if (m_lastAddressInfoError != 0)
    {
      return ::gai_strerror(m_lastAddressInfoError);
    }

    if (m_lastPlatformError != 0)
    {
      return std::strerror(m_lastPlatformError);
    }

    return {};
  }

private:
  [[nodiscard]] static duration_type sanitizeTimeout(duration_type timeout) noexcept
  {
    return (timeout < duration_type::zero()) ? duration_type::zero() : timeout;
  }

  [[nodiscard]] static timeval toTimeval(duration_type timeout) noexcept
  {
    const auto clamped = sanitizeTimeout(timeout);
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(clamped.count() / 1000);
    value.tv_usec = static_cast<decltype(value.tv_usec)>((clamped.count() % 1000) * 1000);
    return value;
  }

  [[nodiscard]] bool applyConfiguredTimeout(duration_type timeout, int optionName) noexcept
  {
    clearLastError();

    if (!isConnected())
    {
      return true;
    }

    const auto timeoutValue = toTimeval(timeout);
    if (::setsockopt(m_socket, SOL_SOCKET, optionName, &timeoutValue, sizeof(timeoutValue)) < 0)
    {
      setLastPlatformError(errno);
      return false;
    }

    return true;
  }

  [[nodiscard]] bool configureSocket(int socketFd) noexcept
  {
#ifdef SO_NOSIGPIPE
    const int disableSigPipe = 1;
    if (::setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &disableSigPipe, sizeof(disableSigPipe)) < 0)
    {
      setLastPlatformError(errno);
      return false;
    }
#endif

    const auto sendTimeoutValue = toTimeval(m_sendTimeout);
    if (::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeoutValue, sizeof(sendTimeoutValue)) < 0)
    {
      setLastPlatformError(errno);
      return false;
    }

    const auto receiveTimeoutValue = toTimeval(m_receiveTimeout);
    if (::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeoutValue, sizeof(receiveTimeoutValue)) < 0)
    {
      setLastPlatformError(errno);
      return false;
    }

    return true;
  }

  [[nodiscard]] static int platformSendFlags() noexcept
  {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
  }

  [[nodiscard]] static bool isTimeoutError(int errorCode) noexcept
  {
    return (errorCode == EAGAIN) || (errorCode == EWOULDBLOCK) || (errorCode == ETIMEDOUT);
  }

  [[nodiscard]] enm_IOResult classifySocketError(int errorCode) noexcept
  {
    setLastPlatformError(errorCode);
    return isTimeoutError(errorCode) ? enm_IOResult::Timeout : enm_IOResult::Failed;
  }

  void clearLastError() noexcept
  {
    m_lastPlatformError = 0;
    m_lastAddressInfoError = 0;
  }

  void setLastPlatformError(int errorCode) noexcept
  {
    m_lastPlatformError = errorCode;
    m_lastAddressInfoError = 0;
  }

  void setLastAddressInfoError(int errorCode) noexcept
  {
    m_lastPlatformError = 0;
    m_lastAddressInfoError = errorCode;
  }

  static void closeSocketValue(int socketFd) noexcept
  {
    if (socketFd >= 0)
    {
      ::shutdown(socketFd, SHUT_RDWR);
      ::close(socketFd);
    }
  }

  std::string m_serverAddress;
  std::uint16_t m_port{0U};
  int m_socket{-1};
  duration_type m_sendTimeout{duration_type::zero()};
  duration_type m_receiveTimeout{duration_type::zero()};
  int m_lastPlatformError{0};
  int m_lastAddressInfoError{0};
};
