#include <iostream>
#include <string>

#include "cHTTPServer.h"

int main()
{
  cHTTPServer server("TestHTTPServer", 8080U, "0.0.0.0");

  (void)server.addResource(
      "^/health$",
      "GET",
      [](const cHTTPServer::st_Request &, cHTTPServer::cResponse &response)
      {
        response.write(
            200U,
            "ok\n",
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  (void)server.addResource(
      "^/echo$",
      "POST",
      [](const cHTTPServer::st_Request &request, cHTTPServer::cResponse &response)
      {
        response.write(
            200U,
            request.body,
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  (void)server.setDefaultResource(
      "GET",
      [](const cHTTPServer::st_Request &, cHTTPServer::cResponse &response)
      {
        response.write(
            404U,
            "Route not found\n",
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  if (!server.startThread(5000))
  {
    std::cerr << "Failed to start HTTP server: " << server.lastError() << std::endl;
    return 1;
  }

  std::cout << "HTTP V1 server listening on port " << server.listeningPort() << std::endl;
  std::cout << "Try: curl http://127.0.0.1:" << server.listeningPort() << "/health" << std::endl;
  std::cout << "Try: curl -X POST http://127.0.0.1:" << server.listeningPort()
            << "/echo -d 'hello'" << std::endl;
  std::cout << "Press ENTER to stop." << std::endl;

  std::string line;
  std::getline(std::cin, line);

  if (!server.stopThread(5000))
  {
    std::cerr << "Failed to stop HTTP server cleanly" << std::endl;
    return 1;
  }

  std::cout << "HTTP V1 server stopped" << std::endl;
  return 0;
}
