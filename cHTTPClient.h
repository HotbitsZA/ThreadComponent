#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class cHTTPClient
{
public:
  struct st_Header
  {
    std::string name;
    std::string value;
  };

  using td_Headers = std::vector<st_Header>;

  enum class enm_Method : std::uint8_t
  {
    Get = 0,
    Post,
    Put,
    Patch,
    Delete,
    Head
  };

  struct st_Request
  {
    std::string url;
    enm_Method method{enm_Method::Get};
    td_Headers headers;
    std::string body;
    std::string userAgent{"HotBits/cHTTPClient"};
    std::string contentType;
    std::string accept;
    std::string basicAuthUser;
    std::string basicAuthPassword;
    std::string bearerToken;
    std::string caInfoPath;
    std::chrono::milliseconds connectTimeout{10000};
    std::chrono::milliseconds timeout{180000};
    long maxRedirects{10};
    bool followRedirects{true};
    bool verifyPeer{true};
    bool verifyHost{true};
    bool acceptCompressedResponse{true};
  };

  struct st_Response
  {
    long statusCode{0};
    std::int32_t curlCode{0};
    std::string body;
    td_Headers headers;
    std::string rawHeaders;
    std::string effectiveUrl;
    std::string errorMessage;
    std::chrono::milliseconds elapsed{0};

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] std::string headerValue(const std::string &name) const;
  };

  cHTTPClient() = default;
  ~cHTTPClient() = default;

  cHTTPClient(const cHTTPClient &) = default;
  cHTTPClient &operator=(const cHTTPClient &) = default;
  cHTTPClient(cHTTPClient &&) noexcept = default;
  cHTTPClient &operator=(cHTTPClient &&) noexcept = default;

  [[nodiscard]] st_Response perform(const st_Request &request) const;

  [[nodiscard]] st_Response get(
      const std::string &url,
      const td_Headers &headers = {}) const;

  [[nodiscard]] st_Response post(
      const std::string &url,
      const std::string &body,
      const td_Headers &headers = {},
      const std::string &contentType = {}) const;

  [[nodiscard]] st_Response put(
      const std::string &url,
      const std::string &body,
      const td_Headers &headers = {},
      const std::string &contentType = {}) const;

  [[nodiscard]] st_Response patch(
      const std::string &url,
      const std::string &body,
      const td_Headers &headers = {},
      const std::string &contentType = {}) const;

  [[nodiscard]] st_Response del(
      const std::string &url,
      const td_Headers &headers = {}) const;

  [[nodiscard]] st_Response head(
      const std::string &url,
      const td_Headers &headers = {}) const;
};
