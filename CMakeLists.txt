cmake_minimum_required(VERSION 3.16)

project(ThreadComponent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(THREADCOMPONENT_BUILD_EXAMPLES "Build ThreadComponent example executables" ON)

get_filename_component(
  THREADCOMPONENT_REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../.."
  ABSOLUTE)
set(
  THREADCOMPONENT_REPOSITORY_ROOT
  "${THREADCOMPONENT_REPOSITORY_ROOT}"
  CACHE PATH
  "Path to the repository root that contains the OpenSource dependency folder")

set(
  THREADCOMPONENT_OPENSOURCE_DIR
  "${THREADCOMPONENT_REPOSITORY_ROOT}/OpenSource"
  CACHE PATH
  "Path to the OpenSource dependency folder")

set(THREADCOMPONENT_SIMPLE_WEB_SERVER_DIR "${THREADCOMPONENT_OPENSOURCE_DIR}/Simple-Web-Server")
set(THREADCOMPONENT_ASIO_DIR "${THREADCOMPONENT_OPENSOURCE_DIR}/asio/asio-1.36.0/include")
set(THREADCOMPONENT_GZIP_DIR "${THREADCOMPONENT_OPENSOURCE_DIR}/gzip/gzip-hpp-0.1.0/include")
set(THREADCOMPONENT_RAPIDJSON_DIR "${THREADCOMPONENT_OPENSOURCE_DIR}/json/rapidjson-1.1.0/include")

foreach(required_dir
    THREADCOMPONENT_SIMPLE_WEB_SERVER_DIR
    THREADCOMPONENT_ASIO_DIR
    THREADCOMPONENT_GZIP_DIR
    THREADCOMPONENT_RAPIDJSON_DIR)
  if(NOT EXISTS "${${required_dir}}")
    message(FATAL_ERROR
      "Required dependency directory not found: ${${required_dir}}\n"
      "Set THREADCOMPONENT_OPENSOURCE_DIR or THREADCOMPONENT_REPOSITORY_ROOT to your dependency tree.")
  endif()
endforeach()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_library(threadcomponent_headers INTERFACE)
target_compile_features(threadcomponent_headers INTERFACE cxx_std_17)
target_include_directories(
  threadcomponent_headers
  INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${THREADCOMPONENT_SIMPLE_WEB_SERVER_DIR}"
    "${THREADCOMPONENT_ASIO_DIR}"
    "${THREADCOMPONENT_GZIP_DIR}"
    "${THREADCOMPONENT_RAPIDJSON_DIR}")
target_link_libraries(threadcomponent_headers INTERFACE Threads::Threads)

add_library(threadcomponent_http_client STATIC cHTTPClient.cpp)
target_link_libraries(threadcomponent_http_client PUBLIC threadcomponent_headers CURL::libcurl)

add_library(threadcomponent_http_server STATIC cHTTPServer.cpp)
target_link_libraries(threadcomponent_http_server PUBLIC threadcomponent_headers)

add_library(threadcomponent_http_server_v2 STATIC cHTTPServer_V2.cpp)
target_link_libraries(threadcomponent_http_server_v2 PUBLIC threadcomponent_headers)

add_library(threadcomponent_https_server STATIC cHTTPSServer.cpp)
target_link_libraries(
  threadcomponent_https_server
  PUBLIC
    threadcomponent_headers
    OpenSSL::SSL
    OpenSSL::Crypto)

add_library(threadcomponent_https_server_v2 STATIC cHTTPSServer_V2.cpp)
target_link_libraries(
  threadcomponent_https_server_v2
  PUBLIC
    threadcomponent_headers
    OpenSSL::SSL
    OpenSSL::Crypto)

if(THREADCOMPONENT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
