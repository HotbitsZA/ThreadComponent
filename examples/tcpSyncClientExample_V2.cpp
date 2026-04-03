#include <cstdint>
#include <iostream>
#include <string>

#include "cTCPSyncClient_V2.h"

namespace
{
  const char *toText(const cTCPSyncClient_V2::enm_IOResult result)
  {
    switch (result)
    {
    case cTCPSyncClient_V2::enm_IOResult::Success:
      return "Success";
    case cTCPSyncClient_V2::enm_IOResult::Closed:
      return "Closed";
    case cTCPSyncClient_V2::enm_IOResult::Timeout:
      return "Timeout";
    case cTCPSyncClient_V2::enm_IOResult::NotConnected:
      return "NotConnected";
    case cTCPSyncClient_V2::enm_IOResult::InvalidArgument:
      return "InvalidArgument";
    case cTCPSyncClient_V2::enm_IOResult::Failed:
    default:
      return "Failed";
    }
  }
}

int main(int argc, char *argv[])
{
  const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
  const std::uint16_t port = (argc > 2) ? static_cast<std::uint16_t>(std::stoi(argv[2])) : 8080U;
  const std::string request = (argc > 3) ? argv[3] : "hello from cTCPSyncClient_V2";

  cTCPSyncClient_V2 client(host, port);
  cTCPSyncClient_V2::Buffer response;

  const auto result = client.exchangeOnceUntilClosed(request, response);

  std::cout << "Result: " << toText(result) << std::endl;
  if (result != cTCPSyncClient_V2::enm_IOResult::Success)
  {
    std::cerr << "Error: " << client.lastErrorMessage() << std::endl;
    return 1;
  }

  std::cout << "Response: " << std::string(response.begin(), response.end()) << std::endl;
  return 0;
}
