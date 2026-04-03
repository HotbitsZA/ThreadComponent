#include "cHTTPClient.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>

#include <curl/curl.h>

namespace
{
  struct CurlEasyHandle
  {
    CURL *handle{nullptr};

    CurlEasyHandle()
        : handle(curl_easy_init())
    {
    }

    ~CurlEasyHandle()
    {
      if (handle != nullptr)
      {
        curl_easy_cleanup(handle);
      }
    }

    CurlEasyHandle(const CurlEasyHandle &) = delete;
    CurlEasyHandle &operator=(const CurlEasyHandle &) = delete;
  };

  struct CurlSListGuard
  {
    curl_slist *list{nullptr};

    CurlSListGuard() = default;
    ~CurlSListGuard()
    {
      if (list != nullptr)
      {
        curl_slist_free_all(list);
      }
    }

    CurlSListGuard(const CurlSListGuard &) = delete;
    CurlSListGuard &operator=(const CurlSListGuard &) = delete;
  };

  [[nodiscard]] std::string trimCopy(std::string_view text)
  {
    std::size_t begin = 0;
    while ((begin < text.size()) && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
      ++begin;
    }

    std::size_t end = text.size();
    while ((end > begin) && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
      --end;
    }

    return std::string(text.substr(begin, end - begin));
  }

  [[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs)
  {
    if (lhs.size() != rhs.size())
    {
      return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
      if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
          std::tolower(static_cast<unsigned char>(rhs[i])))
      {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool hasHeader(const cHTTPClient::td_Headers &headers, std::string_view name)
  {
    return std::any_of(
        headers.begin(),
        headers.end(),
        [name](const cHTTPClient::st_Header &header)
        {
          return iequals(header.name, name);
        });
  }

  void appendHeader(CurlSListGuard &curlHeaders, const cHTTPClient::st_Header &header)
  {
    if (header.name.empty())
    {
      return;
    }

    const std::string rawHeader = header.name + ": " + header.value;
    curlHeaders.list = curl_slist_append(curlHeaders.list, rawHeader.c_str());
  }

  [[nodiscard]] const char *methodToString(cHTTPClient::enm_Method method)
  {
    switch (method)
    {
    case cHTTPClient::enm_Method::Post:
      return "POST";
    case cHTTPClient::enm_Method::Put:
      return "PUT";
    case cHTTPClient::enm_Method::Patch:
      return "PATCH";
    case cHTTPClient::enm_Method::Delete:
      return "DELETE";
    case cHTTPClient::enm_Method::Head:
      return "HEAD";
    case cHTTPClient::enm_Method::Get:
    default:
      return "GET";
    }
  }

  std::once_flag g_curlInitFlag;
  CURLcode g_curlInitResult = CURLE_OK;

  [[nodiscard]] bool ensureCurlGlobalInit(std::string &errorMessage)
  {
    std::call_once(g_curlInitFlag, []()
                   {
    g_curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (g_curlInitResult == CURLE_OK)
    {
      std::atexit([]()
      {
        curl_global_cleanup();
      });
    } });

    if (g_curlInitResult != CURLE_OK)
    {
      errorMessage = curl_easy_strerror(g_curlInitResult);
      return false;
    }

    return true;
  }

  size_t writeBodyCallback(char *data, size_t size, size_t count, void *userData)
  {
    if ((data == nullptr) || (userData == nullptr))
    {
      return 0;
    }

    auto *response = static_cast<cHTTPClient::st_Response *>(userData);
    response->body.append(data, size * count);
    return size * count;
  }

  size_t writeHeaderCallback(char *data, size_t size, size_t count, void *userData)
  {
    if ((data == nullptr) || (userData == nullptr))
    {
      return 0;
    }

    auto *response = static_cast<cHTTPClient::st_Response *>(userData);
    const std::size_t totalBytes = size * count;
    const std::string_view line(data, totalBytes);

    if ((line.size() >= 5U) && (line.substr(0, 5) == "HTTP/"))
    {
      response->headers.clear();
      response->rawHeaders.clear();
    }

    response->rawHeaders.append(data, totalBytes);

    const auto separator = line.find(':');
    if (separator != std::string_view::npos)
    {
      cHTTPClient::st_Header header;
      header.name = trimCopy(line.substr(0, separator));
      header.value = trimCopy(line.substr(separator + 1));
      response->headers.push_back(std::move(header));
    }

    return totalBytes;
  }

  void applyMethod(CURL *curlHandle, const cHTTPClient::st_Request &request)
  {
    switch (request.method)
    {
    case cHTTPClient::enm_Method::Post:
      curl_easy_setopt(curlHandle, CURLOPT_POST, 1L);
      break;

    case cHTTPClient::enm_Method::Put:
    case cHTTPClient::enm_Method::Patch:
    case cHTTPClient::enm_Method::Delete:
      curl_easy_setopt(curlHandle, CURLOPT_CUSTOMREQUEST, methodToString(request.method));
      break;

    case cHTTPClient::enm_Method::Head:
      curl_easy_setopt(curlHandle, CURLOPT_NOBODY, 1L);
      curl_easy_setopt(curlHandle, CURLOPT_CUSTOMREQUEST, methodToString(request.method));
      break;

    case cHTTPClient::enm_Method::Get:
    default:
      curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1L);
      break;
    }
  }
}

bool cHTTPClient::st_Response::ok() const noexcept
{
  return (curlCode == CURLE_OK) && (statusCode >= 200L) && (statusCode < 300L);
}

std::string cHTTPClient::st_Response::headerValue(const std::string &name) const
{
  for (const auto &header : headers)
  {
    if (iequals(header.name, name))
    {
      return header.value;
    }
  }

  return {};
}

cHTTPClient::st_Response cHTTPClient::perform(const st_Request &request) const
{
  st_Response response;

  if (request.url.empty())
  {
    response.errorMessage = "Request URL is empty";
    response.curlCode = CURLE_URL_MALFORMAT;
    return response;
  }

  if (!ensureCurlGlobalInit(response.errorMessage))
  {
    response.curlCode = g_curlInitResult;
    return response;
  }

  CurlEasyHandle curlHandle;
  if (curlHandle.handle == nullptr)
  {
    response.errorMessage = "curl_easy_init failed";
    response.curlCode = CURLE_FAILED_INIT;
    return response;
  }

  std::array<char, CURL_ERROR_SIZE> errorBuffer{};
  errorBuffer.fill('\0');

  CurlSListGuard curlHeaders;

  curl_easy_setopt(curlHandle.handle, CURLOPT_ERRORBUFFER, errorBuffer.data());
  curl_easy_setopt(curlHandle.handle, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curlHandle.handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curlHandle.handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connectTimeout.count()));
  curl_easy_setopt(curlHandle.handle, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));
  curl_easy_setopt(curlHandle.handle, CURLOPT_FOLLOWLOCATION, request.followRedirects ? 1L : 0L);
  curl_easy_setopt(curlHandle.handle, CURLOPT_MAXREDIRS, request.maxRedirects);
  curl_easy_setopt(curlHandle.handle, CURLOPT_SSL_VERIFYPEER, request.verifyPeer ? 1L : 0L);
  curl_easy_setopt(curlHandle.handle, CURLOPT_SSL_VERIFYHOST, request.verifyHost ? 2L : 0L);
  curl_easy_setopt(curlHandle.handle, CURLOPT_WRITEFUNCTION, &writeBodyCallback);
  curl_easy_setopt(curlHandle.handle, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curlHandle.handle, CURLOPT_HEADERFUNCTION, &writeHeaderCallback);
  curl_easy_setopt(curlHandle.handle, CURLOPT_HEADERDATA, &response);

  if (!request.userAgent.empty())
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_USERAGENT, request.userAgent.c_str());
  }

  if (request.acceptCompressedResponse)
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_ACCEPT_ENCODING, "");
  }

  if (!request.caInfoPath.empty())
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_CAINFO, request.caInfoPath.c_str());
  }

  if (!request.bearerToken.empty())
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    curl_easy_setopt(curlHandle.handle, CURLOPT_XOAUTH2_BEARER, request.bearerToken.c_str());
  }
  else if (!request.basicAuthUser.empty())
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curlHandle.handle, CURLOPT_USERNAME, request.basicAuthUser.c_str());
    curl_easy_setopt(curlHandle.handle, CURLOPT_PASSWORD, request.basicAuthPassword.c_str());
  }

  auto requestHeaders = request.headers;

  if (!request.contentType.empty() && !hasHeader(requestHeaders, "Content-Type"))
  {
    requestHeaders.push_back({"Content-Type", request.contentType});
  }

  if (!request.accept.empty() && !hasHeader(requestHeaders, "Accept"))
  {
    requestHeaders.push_back({"Accept", request.accept});
  }

  for (const auto &header : requestHeaders)
  {
    appendHeader(curlHeaders, header);
  }

  if (curlHeaders.list != nullptr)
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_HTTPHEADER, curlHeaders.list);
  }

  applyMethod(curlHandle.handle, request);

  if (!request.body.empty())
  {
    curl_easy_setopt(curlHandle.handle, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(curlHandle.handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }

  const auto startTime = std::chrono::steady_clock::now();
  const CURLcode curlResult = curl_easy_perform(curlHandle.handle);
  response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime);
  response.curlCode = curlResult;

  if (curlResult != CURLE_OK)
  {
    if (errorBuffer[0] != '\0')
    {
      response.errorMessage = errorBuffer.data();
    }
    else
    {
      response.errorMessage = curl_easy_strerror(curlResult);
    }
  }

  curl_easy_getinfo(curlHandle.handle, CURLINFO_RESPONSE_CODE, &response.statusCode);

  char *effectiveUrl = nullptr;
  if (curl_easy_getinfo(curlHandle.handle, CURLINFO_EFFECTIVE_URL, &effectiveUrl) == CURLE_OK &&
      (effectiveUrl != nullptr))
  {
    response.effectiveUrl = effectiveUrl;
  }

  return response;
}

cHTTPClient::st_Response cHTTPClient::get(const std::string &url, const td_Headers &headers) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Get;
  request.headers = headers;
  return perform(request);
}

cHTTPClient::st_Response cHTTPClient::post(
    const std::string &url,
    const std::string &body,
    const td_Headers &headers,
    const std::string &contentType) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Post;
  request.headers = headers;
  request.body = body;
  request.contentType = contentType;
  return perform(request);
}

cHTTPClient::st_Response cHTTPClient::put(
    const std::string &url,
    const std::string &body,
    const td_Headers &headers,
    const std::string &contentType) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Put;
  request.headers = headers;
  request.body = body;
  request.contentType = contentType;
  return perform(request);
}

cHTTPClient::st_Response cHTTPClient::patch(
    const std::string &url,
    const std::string &body,
    const td_Headers &headers,
    const std::string &contentType) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Patch;
  request.headers = headers;
  request.body = body;
  request.contentType = contentType;
  return perform(request);
}

cHTTPClient::st_Response cHTTPClient::del(const std::string &url, const td_Headers &headers) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Delete;
  request.headers = headers;
  return perform(request);
}

cHTTPClient::st_Response cHTTPClient::head(const std::string &url, const td_Headers &headers) const
{
  st_Request request;
  request.url = url;
  request.method = enm_Method::Head;
  request.headers = headers;
  return perform(request);
}
