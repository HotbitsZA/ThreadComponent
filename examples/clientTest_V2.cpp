#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

#include "cTCPClient_V2.h"

namespace
{
  std::mutex g_consoleMutex;

  std::string toText(const TCPClient_V2::Buffer &buffer)
  {
    return std::string(buffer.begin(), buffer.end());
  }
}

int main()
{
  TCPClient_V2 client(
      "TestTCPClient_V2",
      "127.0.0.1",
      8080,
      [](const TCPClient_V2::Buffer &data)
      {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        std::cout << "Server Response: " << toText(data) << std::endl;
      });

  if (!client.startThread(5000))
  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cerr << "Failed to start client" << std::endl;
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << "Client started successfully" << std::endl;
    std::cout << "Type messages and press ENTER to send. Type 'exit' to stop." << std::endl;
  }

  std::string message;
  while (std::getline(std::cin, message))
  {
    if (message == "exit")
    {
      break;
    }

    if (!client.send(message))
    {
      std::lock_guard<std::mutex> lock(g_consoleMutex);
      std::cerr << "Send failed. The client is no longer running." << std::endl;
      break;
    }
  }

  if (!client.stopThread(5000))
  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cerr << "Failed to stop client cleanly" << std::endl;
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << "Client stopped successfully" << std::endl;
  }

  return 0;
}
