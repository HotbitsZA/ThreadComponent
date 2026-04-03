#include <iostream>
#include <string>

#include "cHTTPSServer.h"

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

  cHTTPSServer server(
      "TestHTTPSServer",
      8443U,
      certificateFile,
      privateKeyFile);

  (void)server.addResource(
      "^/health$",
      "GET",
      [](const cHTTPSServer::st_Request &, cHTTPSServer::cResponse &response)
      {
        response.write(
            200U,
            "secure-ok\n",
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  (void)server.addResource(
      "^/echo$",
      "POST",
      [](const cHTTPSServer::st_Request &request, cHTTPSServer::cResponse &response)
      {
        response.write(
            200U,
            request.body,
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  (void)server.setDefaultResource(
      "GET",
      [](const cHTTPSServer::st_Request &, cHTTPSServer::cResponse &response)
      {
        response.write(
            404U,
            "Secure route not found\n",
            {{"Content-Type", "text/plain; charset=utf-8"}});
      });

  if (!server.startThread(5000))
  {
    std::cerr << "Failed to start HTTPS server: " << server.lastError() << std::endl;
    return 1;
  }

  std::cout << "HTTPS V1 server listening on port " << server.listeningPort() << std::endl;
  std::cout << "Certificate: " << certificateFile << std::endl;
  std::cout << "Try: curl -k https://127.0.0.1:" << server.listeningPort() << "/health" << std::endl;
  std::cout << "Try: curl -k -X POST https://127.0.0.1:" << server.listeningPort()
            << "/echo -d 'hello'" << std::endl;
  std::cout << "Press ENTER to stop." << std::endl;

  std::string line;
  std::getline(std::cin, line);

  if (!server.stopThread(5000))
  {
    std::cerr << "Failed to stop HTTPS server cleanly" << std::endl;
    return 1;
  }

  std::cout << "HTTPS V1 server stopped" << std::endl;
  return 0;
}
