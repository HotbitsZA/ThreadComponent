#include <algorithm>
#include <iostream>
#include <string>

#include "cHTTPClient.h"

int main(int argc, char *argv[])
{
  const std::string url = (argc > 1) ? argv[1] : "https://example.com/";

  cHTTPClient client;
  cHTTPClient::st_Request request;
  request.url = url;
  request.accept = "*/*";
  request.userAgent = "HotBits/ThreadComponent/httpClientExample";

  const auto response = client.perform(request);

  std::cout << "URL: " << url << std::endl;
  std::cout << "HTTP status: " << response.statusCode << std::endl;
  std::cout << "curl code: " << response.curlCode << std::endl;
  std::cout << "Elapsed (ms): " << response.elapsed.count() << std::endl;

  if (!response.ok())
  {
    std::cerr << "Request failed: " << response.errorMessage << std::endl;
    return 1;
  }

  const std::size_t previewLength = std::min<std::size_t>(response.body.size(), 300U);
  std::cout << "Body preview:" << std::endl;
  std::cout << response.body.substr(0, previewLength) << std::endl;
  return 0;
}
