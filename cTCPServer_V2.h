#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cBaseWorker_V2.h"

class TCPServer_V2 : public cBaseWorker_V2
{
public:
  using Buffer = std::vector<std::uint8_t>;

  struct ClientInfo
  {
    std::string address;
    std::uint16_t port{0U};
  };

  using DataCallback = std::function<void(const Buffer &, const ClientInfo &)>;

  TCPServer_V2(
      std::string name,
      std::uint16_t port,
      DataCallback callback = {},
      int backlog = 5)
      : cBaseWorker_V2(std::move(name)),
        m_port(port),
        m_backlog(backlog),
        m_dataCallback(std::move(callback))
  {
  }

  ~TCPServer_V2() noexcept override
  {
    (void)stopThread();
    closeServerSocket();
    joinAndClearAllClients();
  }

protected:
  bool preRun() override
  {
    const int serverSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
      return false;
    }

    m_serverSocket.store(serverSocket, std::memory_order_release);

    int reuseAddress = 1;
    ::setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(m_port);

    if (::bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0)
    {
      closeServerSocket();
      return false;
    }

    if (::listen(serverSocket, m_backlog) < 0)
    {
      closeServerSocket();
      return false;
    }

    return true;
  }

  void run() override
  {
    while (continueRunning())
    {
      reapCompletedClients();

      sockaddr_in clientAddr{};
      socklen_t clientLen = sizeof(clientAddr);

      const int serverSocket = m_serverSocket.load(std::memory_order_acquire);
      if (serverSocket < 0)
      {
        break;
      }

      const int clientSocket = ::accept(
          serverSocket,
          reinterpret_cast<sockaddr *>(&clientAddr),
          &clientLen);

      if (clientSocket < 0)
      {
        if ((errno == EINTR) && continueRunning())
        {
          continue;
        }

        if (!stopRequested())
        {
          requestStop();
        }

        break;
      }

      if (!startClientSession(clientSocket, makeClientInfo(clientAddr)))
      {
        closeSocketValue(clientSocket);
      }
    }

    joinAndClearAllClients();
    closeServerSocket();
  }

  void stopTriggered() override
  {
    closeServerSocket();
    closeAllClientSockets();
  }

private:
  struct ClientSession
  {
    explicit ClientSession(int clientSocket, ClientInfo clientInfo)
        : socket(clientSocket),
          info(std::move(clientInfo))
    {
    }

    ClientSession(const ClientSession &) = delete;
    ClientSession &operator=(const ClientSession &) = delete;

    std::atomic<int> socket{-1};
    ClientInfo info;
    std::thread worker;
    std::atomic_bool completed{false};
  };

  static constexpr std::size_t kReadBufferSize = 4096U;

  [[nodiscard]] bool startClientSession(int clientSocket, ClientInfo clientInfo)
  {
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    try
    {
      m_clients.emplace_back(clientSocket, std::move(clientInfo));
    }
    catch (...)
    {
      return false;
    }

    ClientSession *session = &m_clients.back();

    try
    {
      session->worker = std::thread(&TCPServer_V2::handleClient, this, session);
      return true;
    }
    catch (...)
    {
      closeSocket(session->socket);
      m_clients.pop_back();
      return false;
    }
  }

  void handleClient(ClientSession *session) noexcept
  {
    try
    {
      Buffer buffer(kReadBufferSize);

      while (continueRunning())
      {
        const int clientSocket = session->socket.load(std::memory_order_acquire);
        if (clientSocket < 0)
        {
          break;
        }

        const auto bytesRead = ::recv(clientSocket, buffer.data(), buffer.size(), 0);
        if (bytesRead > 0)
        {
          updateHeartbeat();

          if (m_dataCallback)
          {
            Buffer payload(
                buffer.begin(),
                buffer.begin() + static_cast<std::size_t>(bytesRead));
            m_dataCallback(payload, session->info);
          }

          continue;
        }

        if ((bytesRead < 0) && (errno == EINTR))
        {
          continue;
        }

        break;
      }
    }
    catch (...)
    {
    }

    closeSocket(session->socket);
    session->completed.store(true, std::memory_order_release);
  }

  void reapCompletedClients() noexcept
  {
    std::list<ClientSession> completedClients;

    {
      std::lock_guard<std::mutex> lock(m_clientsMutex);
      for (auto it = m_clients.begin(); it != m_clients.end();)
      {
        if (it->completed.load(std::memory_order_acquire))
        {
          auto completedIt = it++;
          completedClients.splice(completedClients.end(), m_clients, completedIt);
          continue;
        }

        ++it;
      }
    }

    for (auto &client : completedClients)
    {
      closeSocket(client.socket);
      if (client.worker.joinable())
      {
        client.worker.join();
      }
    }
  }

  void joinAndClearAllClients() noexcept
  {
    std::list<ClientSession> clientsToJoin;

    {
      std::lock_guard<std::mutex> lock(m_clientsMutex);
      clientsToJoin.splice(clientsToJoin.end(), m_clients);
    }

    for (auto &client : clientsToJoin)
    {
      closeSocket(client.socket);
      if (client.worker.joinable())
      {
        client.worker.join();
      }
    }
  }

  void closeAllClientSockets() noexcept
  {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto &client : m_clients)
    {
      closeSocket(client.socket);
    }
  }

  [[nodiscard]] static ClientInfo makeClientInfo(const sockaddr_in &clientAddr)
  {
    ClientInfo info;
    info.port = ntohs(clientAddr.sin_port);

    char addressBuffer[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &clientAddr.sin_addr, addressBuffer, sizeof(addressBuffer)) != nullptr)
    {
      info.address = addressBuffer;
    }

    return info;
  }

  static void closeSocket(std::atomic<int> &socketValue) noexcept
  {
    const int socket = socketValue.exchange(-1, std::memory_order_acq_rel);
    closeSocketValue(socket);
  }

  static void closeSocketValue(int socket) noexcept
  {
    if (socket >= 0)
    {
      ::shutdown(socket, SHUT_RDWR);
      ::close(socket);
    }
  }

  void closeServerSocket() noexcept
  {
    closeSocket(m_serverSocket);
  }

  std::uint16_t m_port{0U};
  int m_backlog{5};
  DataCallback m_dataCallback;

  std::atomic<int> m_serverSocket{-1};

  std::mutex m_clientsMutex;
  std::list<ClientSession> m_clients;
};
