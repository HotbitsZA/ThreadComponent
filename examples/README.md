# Examples

This folder contains runnable sample programs for the supported ThreadComponent APIs.

## Built By CMake

- `baseWorkerExample_V2.cpp`
  - minimal derived `cBaseWorker_V2` implementation with cooperative shutdown
- `threadQueueExample_V2.cpp`
  - producer/consumer flow using `cThreadQueue_V2`
- `simpleContainerExample_V2.cpp`
  - `simple_container_V2` insert, timed grab, and snapshot usage
- `clientTest_V2.cpp`
  - interactive TCP client using `TCPClient_V2`
- `serverTest_V2.cpp`
  - TCP server using `TCPServer_V2`
- `tcpSyncClientExample_V2.cpp`
  - synchronous request/response use of `cTCPSyncClient_V2`
- `httpClientExample.cpp`
  - outbound HTTP request with `cHTTPClient`
- `httpServerTest.cpp`
  - HTTP server example using `cHTTPServer`
- `httpServerTest_V2.cpp`
  - HTTP server example using `cHTTPServer_V2`
- `httpsServerTest.cpp`
  - HTTPS server example using `cHTTPSServer`
- `httpsServerTest_V2.cpp`
  - HTTPS server example using `cHTTPSServer_V2`

## Legacy Reference Files

These files are intentionally kept as reference material but are not part of the default CMake build:

- `cTestSimpleContainer.cpp`
- `clientTest.cpp`
- `serverTest.cpp`

They depend on older components or repository pieces that are not part of the curated standalone example set.

## TLS Demo Certificates

The HTTPS examples default to the files in:

- `certs/server.crt`
- `certs/server.key`

These are development-only demo files for local testing.
