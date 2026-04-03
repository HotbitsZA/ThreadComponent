#include <iostream>
#include <mutex>
#include <string>

#include "cTCPServer_V2.h"

namespace
{
  std::mutex g_consoleMutex;

  std::string toText(const TCPServer_V2::Buffer &buffer)
  {
    return std::string(buffer.begin(), buffer.end());
  }
}

int main()
{
  TCPServer_V2 server(
      "TestTCPServer_V2",
      8080,
      [](const TCPServer_V2::Buffer &data, const TCPServer_V2::ClientInfo &client)
      {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        std::cout
            << "Received from "
            << client.address
            << ':'
            << client.port
            << " -> "
            << toText(data)
            << std::endl;
      });

  if (!server.startThread(5000))
  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cerr << "Failed to start server" << std::endl;
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << "Server started successfully on port 8080" << std::endl;
    std::cout << "Press ENTER to stop the server." << std::endl;
  }

  std::string line;
  std::getline(std::cin, line);

  if (!server.stopThread(5000))
  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cerr << "Failed to stop server cleanly" << std::endl;
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << "Server stopped successfully" << std::endl;
  }

  return 0;
}
