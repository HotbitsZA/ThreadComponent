#include "cHTTPServer.h"

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <simple-web-server/server_http.hpp>
#include <simple-web-server/status_code.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>

namespace
{
  using HttpServerType = SimpleWeb::Server<SimpleWeb::HTTP>;

  [[nodiscard]] std::string trimCopy(std::string value)
  {
    const auto isSpace = [](unsigned char ch)
    {
      return std::isspace(ch) != 0;
    };

    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [isSpace](unsigned char ch)
            {
              return !isSpace(ch);
            }));

    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [isSpace](unsigned char ch)
            {
              return !isSpace(ch);
            })
            .base(),
        value.end());

    return value;
  }

  [[nodiscard]] std::string normalizeMethod(const std::string &method)
  {
    std::string normalized = trimCopy(method);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char ch)
        {
          return static_cast<char>(std::toupper(ch));
        });
    return normalized;
  }

  [[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs)
  {
    if (lhs.size() != rhs.size())
    {
      return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
      if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
          std::tolower(static_cast<unsigned char>(rhs[index])))
      {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool hasHeader(
      const cHTTPServer::td_Fields &headers,
      std::string_view headerName)
  {
    return std::any_of(
        headers.begin(),
        headers.end(),
        [headerName](const cHTTPServer::st_Field &header)
        {
          return iequals(header.name, headerName);
        });
  }

  [[nodiscard]] std::string makeStatusLine(std::uint16_t statusCode)
  {
    const auto statusEnum = static_cast<SimpleWeb::StatusCode>(statusCode);
    const auto &mapped = SimpleWeb::status_code(statusEnum);
    if (!mapped.empty())
    {
      return mapped;
    }

    return std::to_string(statusCode);
  }

  template <typename EndpointType>
  [[nodiscard]] std::string endpointAddressToString(const EndpointType &endpoint)
  {
    try
    {
      return endpoint.address().to_string();
    }
    catch (...)
    {
      return {};
    }
  }

  template <typename MultiMapType>
  [[nodiscard]] cHTTPServer::td_Fields copyFields(const MultiMapType &fields)
  {
    cHTTPServer::td_Fields copied;
    copied.reserve(fields.size());

    for (const auto &field : fields)
    {
      copied.push_back({field.first, field.second});
    }

    return copied;
  }

  [[nodiscard]] cHTTPServer::st_Request makeRequestSnapshot(
      const std::shared_ptr<HttpServerType::Request> &request)
  {
    cHTTPServer::st_Request snapshot;
    if (!request)
    {
      return snapshot;
    }

    snapshot.method = request->method;
    snapshot.path = request->path;
    snapshot.queryString = request->query_string;
    snapshot.httpVersion = request->http_version;
    snapshot.body = request->content.string();
    snapshot.headers = copyFields(request->header);
    snapshot.queryFields = copyFields(request->parse_query_string());

    snapshot.pathMatch.reserve(request->path_match.size());
    for (const auto &match : request->path_match)
    {
      snapshot.pathMatch.push_back(match.str());
    }

    const auto remoteEndpoint = request->remote_endpoint();
    snapshot.remoteAddress = endpointAddressToString(remoteEndpoint);
    snapshot.remotePort = remoteEndpoint.port();

    const auto localEndpoint = request->local_endpoint();
    snapshot.localAddress = endpointAddressToString(localEndpoint);
    snapshot.localPort = localEndpoint.port();

    return snapshot;
  }

  void writeResponse(
      const std::shared_ptr<HttpServerType::Response> &response,
      std::uint16_t statusCode,
      std::string_view body,
      const cHTTPServer::td_Fields &headers)
  {
    if (!response)
    {
      return;
    }

    *response << "HTTP/1.1 " << makeStatusLine(statusCode) << "\r\n";

    for (const auto &header : headers)
    {
      if (header.name.empty())
      {
        continue;
      }

      *response << header.name << ": " << header.value << "\r\n";
    }

    if (!hasHeader(headers, "Content-Length"))
    {
      *response << "Content-Length: " << body.size() << "\r\n";
    }

    *response << "\r\n";

    if (!body.empty())
    {
      response->write(body.data(), static_cast<std::streamsize>(body.size()));
    }
  }
}

struct cHTTPServer::cResponse::st_State
{
  using td_WriteFunction =
      std::function<void(std::uint16_t, std::string_view, const td_Fields &)>;

  explicit st_State(td_WriteFunction writerIn)
      : writer(std::move(writerIn))
  {
  }

  td_WriteFunction writer;
  std::atomic_bool responded{false};
};

struct cHTTPServer::st_Impl
{
  struct st_Route
  {
    std::string pathRegex;
    std::string method;
    td_RequestHandler handler;
  };

  mutable std::mutex mutex;
  std::condition_variable startCondition;
  std::vector<st_Route> resources;
  std::vector<st_Route> defaultResources;
  td_ErrorHandler errorHandler;
  std::unique_ptr<HttpServerType> server;
  std::string bindAddress;
  std::uint16_t configuredPort{0U};
  std::uint16_t listeningPort{0U};
  std::size_t threadPoolSize{1U};
  bool reuseAddress{true};
  bool fastOpen{false};
  long requestTimeoutSeconds{5L};
  long contentTimeoutSeconds{300L};
  std::size_t maxRequestSize{(std::numeric_limits<std::size_t>::max)()};
  bool startResolved{false};
  bool startSucceeded{false};
  std::string lastError;

  void resetStartState()
  {
    std::lock_guard<std::mutex> lock(mutex);
    startResolved = false;
    startSucceeded = false;
    listeningPort = 0U;
    lastError.clear();
  }

  void resolveStartIfPending(
      bool success,
      std::uint16_t port,
      std::string errorMessage = {})
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (startResolved)
      {
        if (!errorMessage.empty())
        {
          lastError = std::move(errorMessage);
        }
        return;
      }

      startResolved = true;
      startSucceeded = success;
      listeningPort = success ? port : 0U;
      lastError = std::move(errorMessage);
    }

    startCondition.notify_all();
  }

  void setLastError(std::string errorMessage)
  {
    std::lock_guard<std::mutex> lock(mutex);
    lastError = std::move(errorMessage);
  }
};

cHTTPServer::cResponse::cResponse(std::shared_ptr<st_State> state)
    : m_state(std::move(state))
{
}

cHTTPServer::cResponse::~cResponse()
{
  if (m_state && !m_state->responded.load(std::memory_order_acquire) &&
      (m_state.use_count() == 1U))
  {
    write(204U, td_Fields{});
  }
}

void cHTTPServer::cResponse::write(std::string_view body) const
{
  write(200U, body, {});
}

void cHTTPServer::cResponse::write(
    std::uint16_t statusCode,
    std::string_view body,
    const td_Fields &headers) const
{
  if (!m_state)
  {
    return;
  }

  const bool alreadyResponded =
      m_state->responded.exchange(true, std::memory_order_acq_rel);
  if (alreadyResponded)
  {
    return;
  }

  m_state->writer(statusCode, body, headers);
}

void cHTTPServer::cResponse::write(
    std::uint16_t statusCode,
    const td_Fields &headers) const
{
  write(statusCode, std::string_view{}, headers);
}

void cHTTPServer::cResponse::redirect(
    const std::string &location,
    std::uint16_t statusCode) const
{
  write(statusCode, std::string_view{}, {{"Location", location}});
}

bool cHTTPServer::cResponse::valid() const noexcept
{
  return static_cast<bool>(m_state);
}

cHTTPServer::cHTTPServer(
    std::string name,
    std::uint16_t port,
    std::string bindAddress,
    std::size_t threadPoolSize)
    : cBaseWorker_V2(std::move(name)),
      m_impl(std::make_unique<st_Impl>())
{
  m_impl->configuredPort = port;
  m_impl->bindAddress = std::move(bindAddress);
  m_impl->threadPoolSize = (threadPoolSize == 0U) ? 1U : threadPoolSize;
}

cHTTPServer::~cHTTPServer() noexcept
{
  (void)stopThread();
}

bool cHTTPServer::addResource(
    const std::string &pathRegex,
    const std::string &method,
    td_RequestHandler handler)
{
  const auto normalizedMethod = normalizeMethod(method);
  if (pathRegex.empty() || normalizedMethod.empty() || !handler || isRunning())
  {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  auto it = std::find_if(
      m_impl->resources.begin(),
      m_impl->resources.end(),
      [&pathRegex, &normalizedMethod](const st_Impl::st_Route &route)
      {
        return (route.pathRegex == pathRegex) && (route.method == normalizedMethod);
      });

  if (it != m_impl->resources.end())
  {
    it->handler = std::move(handler);
    return true;
  }

  m_impl->resources.push_back({pathRegex, normalizedMethod, std::move(handler)});
  return true;
}

bool cHTTPServer::setDefaultResource(
    const std::string &method,
    td_RequestHandler handler)
{
  const auto normalizedMethod = normalizeMethod(method);
  if (normalizedMethod.empty() || !handler || isRunning())
  {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  auto it = std::find_if(
      m_impl->defaultResources.begin(),
      m_impl->defaultResources.end(),
      [&normalizedMethod](const st_Impl::st_Route &route)
      {
        return route.method == normalizedMethod;
      });

  if (it != m_impl->defaultResources.end())
  {
    it->handler = std::move(handler);
    return true;
  }

  m_impl->defaultResources.push_back({"", normalizedMethod, std::move(handler)});
  return true;
}

void cHTTPServer::setErrorHandler(td_ErrorHandler handler)
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->errorHandler = std::move(handler);
}

void cHTTPServer::clearErrorHandler()
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->errorHandler = {};
}

void cHTTPServer::setBindAddress(std::string bindAddress)
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->bindAddress = std::move(bindAddress);
}

void cHTTPServer::setPort(std::uint16_t port) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->configuredPort = port;
}

void cHTTPServer::setThreadPoolSize(std::size_t threadPoolSize) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->threadPoolSize = (threadPoolSize == 0U) ? 1U : threadPoolSize;
}

void cHTTPServer::setReuseAddress(bool reuseAddress) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->reuseAddress = reuseAddress;
}

void cHTTPServer::setFastOpen(bool fastOpen) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->fastOpen = fastOpen;
}

void cHTTPServer::setRequestTimeoutSeconds(long timeoutSeconds) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->requestTimeoutSeconds = std::max(0L, timeoutSeconds);
}

void cHTTPServer::setContentTimeoutSeconds(long timeoutSeconds) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->contentTimeoutSeconds = std::max(0L, timeoutSeconds);
}

void cHTTPServer::setMaxRequestSize(std::size_t maxRequestSize) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->maxRequestSize =
      (maxRequestSize == 0U) ? (std::numeric_limits<std::size_t>::max)() : maxRequestSize;
}

std::string cHTTPServer::bindAddress() const
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->bindAddress;
}

std::uint16_t cHTTPServer::configuredPort() const noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->configuredPort;
}

std::uint16_t cHTTPServer::listeningPort() const noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->listeningPort;
}

std::size_t cHTTPServer::threadPoolSize() const noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->threadPoolSize;
}

std::string cHTTPServer::lastError() const
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->lastError;
}

bool cHTTPServer::startThread(duration_type waitForStartTimeout)
{
  m_impl->resetStartState();

  if (!cBaseWorker_V2::startThread(duration_type::zero()))
  {
    return false;
  }

  return waitUntilListening(waitForStartTimeout);
}

bool cHTTPServer::startThread(std::uint16_t waitForStartTimeoutMilliSec)
{
  return startThread(duration_type{waitForStartTimeoutMilliSec});
}

bool cHTTPServer::preRun()
{
  try
  {
    std::vector<st_Impl::st_Route> resources;
    std::vector<st_Impl::st_Route> defaultResources;
    td_ErrorHandler errorHandler;

    {
      std::lock_guard<std::mutex> lock(m_impl->mutex);
      resources = m_impl->resources;
      defaultResources = m_impl->defaultResources;
      errorHandler = m_impl->errorHandler;

      m_impl->server = std::make_unique<HttpServerType>();
      m_impl->server->config.port = m_impl->configuredPort;
      m_impl->server->config.address = m_impl->bindAddress;
      m_impl->server->config.thread_pool_size = m_impl->threadPoolSize;
      m_impl->server->config.reuse_address = m_impl->reuseAddress;
      m_impl->server->config.fast_open = m_impl->fastOpen;
      m_impl->server->config.timeout_request = m_impl->requestTimeoutSeconds;
      m_impl->server->config.timeout_content = m_impl->contentTimeoutSeconds;
      m_impl->server->config.max_request_streambuf_size = m_impl->maxRequestSize;
    }

    for (const auto &route : resources)
    {
      m_impl->server->resource[route.pathRegex][route.method] =
          [this, handler = route.handler, errorHandler](
              std::shared_ptr<HttpServerType::Response> response,
              std::shared_ptr<HttpServerType::Request> request)
      {
        updateHeartbeat();

        auto requestSnapshot = makeRequestSnapshot(request);
        cResponse wrappedResponse(std::make_shared<cResponse::st_State>(
            [response](std::uint16_t statusCode, std::string_view body, const td_Fields &headers)
            {
              writeResponse(response, statusCode, body, headers);
            }));

        try
        {
          handler(requestSnapshot, wrappedResponse);
        }
        catch (const std::exception &error)
        {
          m_impl->setLastError(error.what());
          if (errorHandler)
          {
            errorHandler(requestSnapshot, -1, error.what());
          }
          wrappedResponse.write(
              500U,
              error.what(),
              {{"Content-Type", "text/plain; charset=utf-8"}});
        }
        catch (...)
        {
          const std::string message = "Unhandled request handler exception";
          m_impl->setLastError(message);
          if (errorHandler)
          {
            errorHandler(requestSnapshot, -1, message);
          }
          wrappedResponse.write(
              500U,
              message,
              {{"Content-Type", "text/plain; charset=utf-8"}});
        }
      };
    }

    for (const auto &route : defaultResources)
    {
      m_impl->server->default_resource[route.method] =
          [this, handler = route.handler, errorHandler](
              std::shared_ptr<HttpServerType::Response> response,
              std::shared_ptr<HttpServerType::Request> request)
      {
        updateHeartbeat();

        auto requestSnapshot = makeRequestSnapshot(request);
        cResponse wrappedResponse(std::make_shared<cResponse::st_State>(
            [response](std::uint16_t statusCode, std::string_view body, const td_Fields &headers)
            {
              writeResponse(response, statusCode, body, headers);
            }));

        try
        {
          handler(requestSnapshot, wrappedResponse);
        }
        catch (const std::exception &error)
        {
          m_impl->setLastError(error.what());
          if (errorHandler)
          {
            errorHandler(requestSnapshot, -1, error.what());
          }
          wrappedResponse.write(
              500U,
              error.what(),
              {{"Content-Type", "text/plain; charset=utf-8"}});
        }
        catch (...)
        {
          const std::string message = "Unhandled default handler exception";
          m_impl->setLastError(message);
          if (errorHandler)
          {
            errorHandler(requestSnapshot, -1, message);
          }
          wrappedResponse.write(
              500U,
              message,
              {{"Content-Type", "text/plain; charset=utf-8"}});
        }
      };
    }

    m_impl->server->on_error =
        [this, errorHandler](
            std::shared_ptr<HttpServerType::Request> request,
            const SimpleWeb::error_code &error)
    {
      updateHeartbeat();

      const auto requestSnapshot = makeRequestSnapshot(request);
      const std::string message = error.message();
      m_impl->setLastError(message);
      if (errorHandler)
      {
        errorHandler(requestSnapshot, error.value(), message);
      }
    };

    return true;
  }
  catch (const std::exception &error)
  {
    m_impl->resolveStartIfPending(false, 0U, error.what());
  }
  catch (...)
  {
    m_impl->resolveStartIfPending(false, 0U, "Failed to initialize HTTP server");
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->server.reset();
  return false;
}

void cHTTPServer::run()
{
  try
  {
    HttpServerType *server = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_impl->mutex);
      server = m_impl->server.get();
    }

    if (server == nullptr)
    {
      m_impl->resolveStartIfPending(false, 0U, "HTTP server is not initialized");
      return;
    }

    server->start([this](unsigned short port)
                  {
      updateHeartbeat();
      m_impl->resolveStartIfPending(true, port, {}); });

    m_impl->resolveStartIfPending(
        false,
        0U,
        stopRequested() ? "HTTP server stopped" : "HTTP server exited unexpectedly");
  }
  catch (const std::exception &error)
  {
    m_impl->resolveStartIfPending(false, 0U, error.what());
    throw;
  }
  catch (...)
  {
    m_impl->resolveStartIfPending(false, 0U, "Unhandled HTTP server exception");
    throw;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->server.reset();
}

void cHTTPServer::stopTriggered()
{
  HttpServerType *server = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    server = m_impl->server.get();
  }

  if (server != nullptr)
  {
    server->stop();
  }

  m_impl->resolveStartIfPending(false, 0U, "HTTP server stop requested");
}

bool cHTTPServer::waitUntilListening(duration_type waitForStartTimeout)
{
  if (waitForStartTimeout == duration_type::zero())
  {
    return true;
  }

  std::unique_lock<std::mutex> lock(m_impl->mutex);
  const bool completed = m_impl->startCondition.wait_for(
      lock,
      waitForStartTimeout,
      [this]()
      {
        return m_impl->startResolved;
      });

  if (!completed)
  {
    return false;
  }

  return m_impl->startSucceeded;
}
