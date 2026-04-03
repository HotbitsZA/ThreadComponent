#include <iostream>
#include <string>

#include "cHTTPSServer_V2.h"

namespace
{
#ifndef THREADCOMPONENT_EXAMPLES_CERT_DIR
#define THREADCOMPONENT_EXAMPLES_CERT_DIR "."
#endif

  constexpr const char *kDefaultCertificate =
      THREADCOMPONENT_EXAMPLES_CERT_DIR "/server.crt";
  constexpr const char *kDefaultPrivateKey =
      THREADCOMPONENT_EXAMPLES_CERT_DIR "/server.key";
}

int main(int argc, char *argv[])
{
  const std::string certificateFile = (argc > 1) ? argv[1] : kDefaultCertificate;
  const std::string privateKeyFile = (argc > 2) ? argv[2] : kDefaultPrivateKey;

  cHTTPSServer_V2 server(
      "TestHTTPSServer_V2",
      8444U,
      certificateFile,
      privateKeyFile,
      {},
      2U);

  server.server().resource["^/health$"]["GET"] =
      [](cHTTPSServer_V2::response_ptr response, cHTTPSServer_V2::request_ptr)
  {
    response->write("secure-ok-v2\n", {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().resource["^/echo$"]["POST"] =
      [](cHTTPSServer_V2::response_ptr response, cHTTPSServer_V2::request_ptr request)
  {
    response->write(
        request->content.string(),
        {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().default_resource["GET"] =
      [](cHTTPSServer_V2::response_ptr response, cHTTPSServer_V2::request_ptr)
  {
    response->write(
        SimpleWeb::StatusCode::client_error_not_found,
        "Secure route not found\n",
        {{"Content-Type", "text/plain; charset=utf-8"}});
  };

  server.server().on_error =
      [](cHTTPSServer_V2::request_ptr request, const SimpleWeb::error_code &error)
  {
    const std::string path = request ? request->path : "<unknown>";
    std::cerr << "HTTPS V2 error on [" << path << "]: " << error.message() << std::endl;
  };

  if (!server.startThread(5000))
  {
    std::cerr << "Failed to start HTTPS V2 server: " << server.lastError() << std::endl;
    return 1;
  }

  std::cout << "HTTPS V2 server listening on port " << server.listeningPort() << std::endl;
  std::cout << "Certificate: " << certificateFile << std::endl;
  std::cout << "Try: curl -k https://127.0.0.1:" << server.listeningPort() << "/health" << std::endl;
  std::cout << "Try: curl -k -X POST https://127.0.0.1:" << server.listeningPort()
            << "/echo -d 'hello'" << std::endl;
  std::cout << "Press ENTER to stop." << std::endl;

  std::string line;
  std::getline(std::cin, line);

  if (!server.stopThread(5000))
  {
    std::cerr << "Failed to stop HTTPS V2 server cleanly" << std::endl;
    return 1;
  }

  std::cout << "HTTPS V2 server stopped" << std::endl;
  return 0;
}
