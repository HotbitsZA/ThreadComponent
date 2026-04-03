# ThreadComponent

ThreadComponent is a small C++17 networking and concurrency toolkit from Hotbits. It collects reusable worker, queue, container, TCP, HTTP, and HTTPS components that are designed to be practical in service-style applications and easy to embed in larger projects.

The current focus is the V2 API set:

- `cBaseWorker_V2` for cooperative background workers
- `cThreadQueue_V2` for simple thread-safe queued handoff
- `simple_container_V2` for thread-safe sequence-style buffering
- `TCPClient_V2`, `TCPServer_V2`, and `cTCPSyncClient_V2` for TCP client/server use cases
- `cHTTPServer_V2` and `cHTTPSServer_V2` for Simple-Web-Server based HTTP services
- `cHTTPClient` for outbound HTTP requests with libcurl

Legacy headers are still present for compatibility, but the maintained examples and build targets are centered on the V2 classes and the currently self-contained components.

## Requirements

- C++17 compiler
- CMake 3.16+
- OpenSSL development package
- libcurl development package
- POSIX-style sockets environment
  - tested shape is macOS/Linux
- The sibling `OpenSource` dependency tree expected by this project:
  - `OpenSource/Simple-Web-Server`
  - `OpenSource/asio/asio-1.36.0/include`
  - `OpenSource/gzip/gzip-hpp-0.1.0/include`
  - `OpenSource/json/rapidjson-1.1.0/include`

By default the root `CMakeLists.txt` assumes this folder lives under:

`<repo>/Software/ThreadComponent`

and that dependencies live under:

`<repo>/OpenSource`

If your checkout layout is different, pass `THREADCOMPONENT_REPOSITORY_ROOT` or `THREADCOMPONENT_OPENSOURCE_DIR` to CMake.

## Build

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

If the `OpenSource` folder is somewhere else:

```bash
cmake -S . -B build -DTHREADCOMPONENT_OPENSOURCE_DIR=/path/to/OpenSource
cmake --build build
```

Example binaries are written to:

`build/bin/`

## Example Targets

The curated examples built by CMake are:

- `baseWorkerExample_V2.bin`
- `threadQueueExample_V2.bin`
- `simpleContainerExample_V2.bin`
- `clientTest_V2.bin`
- `serverTest_V2.bin`
- `tcpSyncClientExample_V2.bin`
- `httpClientExample.bin`
- `httpServerTest.bin`
- `httpServerTest_V2.bin`
- `httpsServerTest.bin`
- `httpsServerTest_V2.bin`

The HTTPS examples use the sample certificate files in `examples/certs/`.

## Running Examples

Examples are intentionally small and self-contained:

- `baseWorkerExample_V2.bin`
  - starts a derived worker, emits heartbeat updates, then stops cleanly
- `threadQueueExample_V2.bin`
  - shows producer/consumer style queue handoff
- `simpleContainerExample_V2.bin`
  - demonstrates batch insert, timed grab, and snapshot usage
- `serverTest_V2.bin`
  - starts a TCP server on port `8080`
- `clientTest_V2.bin`
  - connects to `127.0.0.1:8080` and sends interactive messages
- `tcpSyncClientExample_V2.bin`
  - performs one synchronous TCP request/response exchange
- `httpClientExample.bin`
  - sends an outbound HTTP request with `cHTTPClient`
- `httpServerTest.bin` and `httpServerTest_V2.bin`
  - start HTTP servers with `/health` and `/echo` routes
- `httpsServerTest.bin` and `httpsServerTest_V2.bin`
  - start HTTPS servers with the bundled demo certificate

The `examples/README.md` file describes each example in a little more detail.

## Project Layout

- `cBaseWorker_V2.h`
  - RAII-oriented worker thread base class
- `cThreadQueue_V2.h`
  - thread-safe queue wrapper
- `simple_container_V2.h`
  - thread-safe sequence container
- `cTCPClient_V2.h`
  - asynchronous TCP client
- `cTCPServer_V2.h`
  - asynchronous TCP server
- `cTCPSyncClient_V2.h`
  - synchronous request/response TCP client
- `cHTTPClient.h` / `cHTTPClient.cpp`
  - libcurl-backed HTTP client
- `cHTTPServer*.h/.cpp`
  - HTTP server wrappers
- `cHTTPSServer*.h/.cpp`
  - HTTPS server wrappers
- `examples/`
  - runnable sample programs and TLS demo certs

## Notes

- Not every legacy source file in this folder is built by default.
- The old `clientTest.cpp`, `serverTest.cpp`, and `cTestSimpleContainer.cpp` are retained as reference material, but the supported example set is the curated one wired into `examples/CMakeLists.txt`.
- This project currently targets POSIX networking APIs. Windows support would need additional socket abstraction work.

## License

This project is licensed under the GNU GPL v3.0. See `LICENSE`.

Third-party libraries and headers keep their own licenses.
