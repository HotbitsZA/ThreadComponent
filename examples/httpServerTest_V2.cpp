#include <iostream>
#include <string>

#include "cHTTPServer_V2.h"

int main()
{
  cHTTPServer_V2 server("TestHTTPServer_V2", 8081U, 2U);

  server.server().resource["^/health$"]["GET"] =
      [](cHTTPServer_V2::response_ptr response, cHTTPServer_V2::request_ptr)
  {
    response->write("ok-v2\n", {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().resource["^/echo$"]["POST"] =
      [](cHTTPServer_V2::response_ptr response, cHTTPServer_V2::request_ptr request)
  {
    response->write(
        request->content.string(),
        {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().default_resource["GET"] =
      [](cHTTPServer_V2::response_ptr response, cHTTPServer_V2::request_ptr)
  {
    response->write(
        SimpleWeb::StatusCode::client_error_not_found,
        "Route not found\n",
        {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().on_error =
      [](cHTTPServer_V2::request_ptr request, const SimpleWeb::error_code &error)
  {
    const std::string path = request ? request->path : "<unknown>";
    std::cerr << "HTTP V2 error on [" << path << "]: " << error.message() << std::endl;
  };

  if (!server.startThread(5000))
  {
    std::cerr << "Failed to start HTTP V2 server: " << server.lastError() << std::endl;
    return 1;
  }

  std::cout << "HTTP V2 server listening on port " << server.listeningPort() << std::endl;
  std::cout << "Try: curl http://127.0.0.1:" << server.listeningPort() << "/health" << std::endl;
  std::cout << "Try: curl -X POST http://127.0.0.1:" << server.listeningPort()
            << "/echo -d 'hello'" << std::endl;
  std::cout << "Press ENTER to stop." << std::endl;

  std::string line;
  std::getline(std::cin, line);

  if (!server.stopThread(5000))
  {
    std::cerr << "Failed to stop HTTP V2 server cleanly" << std::endl;
    return 1;
  }

  std::cout << "HTTP V2 server stopped" << std::endl;
  return 0;
}
