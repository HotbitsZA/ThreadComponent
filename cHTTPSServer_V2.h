#pragma once

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <simple-web-server/server_https.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cBaseWorker_V2.h"

/**
  cHTTPSServer_V2

  Direct Simple-Web-Server HTTPS wrapper integrated with cBaseWorker_V2.

  Configure routes and server settings through `server()` before calling
  `startThread()`. This wrapper owns the asio io_context and runs it on an
  external thread pool controlled by the worker lifecycle.
*/
class cHTTPSServer_V2 : public cBaseWorker_V2
{
  public:
    using server_type = SimpleWeb::Server<SimpleWeb::HTTPS>;
    using request_type = server_type::Request;
    using response_type = server_type::Response;
    using request_ptr = std::shared_ptr<request_type>;
    using response_ptr = std::shared_ptr<response_type>;
    using error_handler_type =
      std::function<void(request_ptr, const SimpleWeb::error_code&)>;

    cHTTPSServer_V2(
      std::string name,
      std::uint16_t port,
      std::string certificateFile,
      std::string privateKeyFile,
      std::string verifyFile = {},
      std::size_t ioThreadCount = 1U);
    ~cHTTPSServer_V2() noexcept override;

    cHTTPSServer_V2(const cHTTPSServer_V2&) = delete;
    cHTTPSServer_V2& operator=(const cHTTPSServer_V2&) = delete;
    cHTTPSServer_V2(cHTTPSServer_V2&&) = delete;
    cHTTPSServer_V2& operator=(cHTTPSServer_V2&&) = delete;

    [[nodiscard]] server_type& server() noexcept;
    [[nodiscard]] const server_type& server() const noexcept;
    [[nodiscard]] std::shared_ptr<SimpleWeb::io_context> ioContext() const noexcept;

    void setIOThreadCount(std::size_t ioThreadCount) noexcept;
    [[nodiscard]] std::size_t ioThreadCount() const noexcept;

    [[nodiscard]] std::uint16_t configuredPort() const noexcept;
    [[nodiscard]] std::string bindAddress() const;
    [[nodiscard]] std::uint16_t listeningPort() const noexcept;
    [[nodiscard]] std::string certificateFile() const noexcept;
    [[nodiscard]] std::string privateKeyFile() const noexcept;
    [[nodiscard]] std::string verifyFile() const noexcept;
    [[nodiscard]] std::string lastError() const;

    [[nodiscard]] bool startThread(
      duration_type waitForStartTimeout = kDefaultWaitTimeout);
    [[nodiscard]] bool startThread(std::uint16_t waitForStartTimeoutMilliSec);

  protected:
    bool preRun() override;
    void run() override;
    void stopTriggered() override;

  private:
    void processWork();
    void resetStartState();
    void resolveStartIfPending(
      bool success,
      std::uint16_t port,
      std::string errorMessage = {});
    void setLastError(std::string errorMessage);
    [[nodiscard]] bool waitUntilListening(duration_type waitForStartTimeout);

    std::unique_ptr<server_type> m_server;
    std::shared_ptr<SimpleWeb::io_context> m_io;
    std::vector<std::thread> m_ioThreads;
    error_handler_type m_userOnError;

    mutable std::mutex m_mutex;
    std::condition_variable m_startCondition;
    std::size_t m_ioThreadCount{1U};
    std::uint16_t m_listeningPort{0U};
    bool m_startResolved{false};
    bool m_startSucceeded{false};
    std::string m_lastError;
    const std::string m_certificateFile;
    const std::string m_privateKeyFile;
    const std::string m_verifyFile;
};
