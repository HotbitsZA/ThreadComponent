#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cBaseWorker_V2.h"

class cHTTPSServer : public cBaseWorker_V2
{
  public:
    struct st_Field
    {
      std::string name;
      std::string value;
    };

    using td_Fields = std::vector<st_Field>;

    struct st_Request
    {
      std::string method;
      std::string path;
      std::string queryString;
      std::string httpVersion;
      std::string body;
      td_Fields headers;
      td_Fields queryFields;
      std::vector<std::string> pathMatch;
      std::string remoteAddress;
      std::uint16_t remotePort{0U};
      std::string localAddress;
      std::uint16_t localPort{0U};
    };

    class cResponse
    {
      public:
        cResponse() = default;
        ~cResponse();

        cResponse(const cResponse&) = default;
        cResponse& operator=(const cResponse&) = default;
        cResponse(cResponse&&) noexcept = default;
        cResponse& operator=(cResponse&&) noexcept = default;

        void write(std::string_view body) const;
        void write(
          std::uint16_t statusCode,
          std::string_view body,
          const td_Fields& headers = {}) const;
        void write(std::uint16_t statusCode, const td_Fields& headers) const;
        void redirect(
          const std::string& location,
          std::uint16_t statusCode = 302U) const;
        [[nodiscard]] bool valid() const noexcept;

      private:
        struct st_State;

        explicit cResponse(std::shared_ptr<st_State> state);

        std::shared_ptr<st_State> m_state;

        friend class cHTTPSServer;
    };

    using td_RequestHandler = std::function<void(const st_Request&, cResponse&)>;
    using td_ErrorHandler = std::function<void(const st_Request&, int, const std::string&)>;

    cHTTPSServer(
      std::string name,
      std::uint16_t port,
      std::string certificateFile,
      std::string privateKeyFile,
      std::string verifyFile = {},
      std::string bindAddress = {},
      std::size_t threadPoolSize = 1U);
    ~cHTTPSServer() noexcept override;

    cHTTPSServer(const cHTTPSServer&) = delete;
    cHTTPSServer& operator=(const cHTTPSServer&) = delete;
    cHTTPSServer(cHTTPSServer&&) = delete;
    cHTTPSServer& operator=(cHTTPSServer&&) = delete;

    [[nodiscard]] bool addResource(
      const std::string& pathRegex,
      const std::string& method,
      td_RequestHandler handler);

    [[nodiscard]] bool setDefaultResource(
      const std::string& method,
      td_RequestHandler handler);

    void setErrorHandler(td_ErrorHandler handler);
    void clearErrorHandler();

    void setBindAddress(std::string bindAddress);
    void setPort(std::uint16_t port) noexcept;
    void setThreadPoolSize(std::size_t threadPoolSize) noexcept;
    void setReuseAddress(bool reuseAddress) noexcept;
    void setFastOpen(bool fastOpen) noexcept;
    void setRequestTimeoutSeconds(long timeoutSeconds) noexcept;
    void setContentTimeoutSeconds(long timeoutSeconds) noexcept;
    void setMaxRequestSize(std::size_t maxRequestSize) noexcept;
    void setCertificateFiles(
      std::string certificateFile,
      std::string privateKeyFile,
      std::string verifyFile = {});

    [[nodiscard]] std::string bindAddress() const;
    [[nodiscard]] std::string certificateFile() const;
    [[nodiscard]] std::string privateKeyFile() const;
    [[nodiscard]] std::string verifyFile() const;
    [[nodiscard]] std::uint16_t configuredPort() const noexcept;
    [[nodiscard]] std::uint16_t listeningPort() const noexcept;
    [[nodiscard]] std::size_t threadPoolSize() const noexcept;
    [[nodiscard]] std::string lastError() const;

    [[nodiscard]] bool startThread(
      duration_type waitForStartTimeout = kDefaultWaitTimeout);
    [[nodiscard]] bool startThread(std::uint16_t waitForStartTimeoutMilliSec);

  protected:
    bool preRun() override;
    void run() override;
    void stopTriggered() override;

  private:
    struct st_Impl;

    [[nodiscard]] bool waitUntilListening(duration_type waitForStartTimeout);

    std::unique_ptr<st_Impl> m_impl;
};
