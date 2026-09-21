function(add_threadcomponent_example target_name source_file)
  add_executable("${target_name}" "${CMAKE_CURRENT_LIST_DIR}/${source_file}")
  set_target_properties("${target_name}" PROPERTIES OUTPUT_NAME "${target_name}.bin")
  target_link_libraries("${target_name}" PRIVATE threadcomponent_headers)
endfunction()

function(add_threadcomponent_http_example target_name source_file library_target)
  add_executable("${target_name}" "${CMAKE_CURRENT_LIST_DIR}/${source_file}")
  set_target_properties("${target_name}" PROPERTIES OUTPUT_NAME "${target_name}.bin")
  target_link_libraries("${target_name}" PRIVATE "${library_target}")
endfunction()

function(add_threadcomponent_https_example target_name source_file library_target)
  add_threadcomponent_http_example("${target_name}" "${source_file}" "${library_target}")
  target_compile_definitions(
    "${target_name}"
    PRIVATE
      THREADCOMPONENT_EXAMPLES_CERT_DIR="${CMAKE_CURRENT_LIST_DIR}/certs")
endfunction()

add_threadcomponent_example("baseWorkerExample_V2" "baseWorkerExample_V2.cpp")
add_threadcomponent_example("threadQueueExample_V2" "threadQueueExample_V2.cpp")
add_threadcomponent_example("simpleContainerExample_V2" "simpleContainerExample_V2.cpp")
add_threadcomponent_example("clientTest_V2" "clientTest_V2.cpp")
add_threadcomponent_example("serverTest_V2" "serverTest_V2.cpp")
add_threadcomponent_example("tcpSyncClientExample_V2" "tcpSyncClientExample_V2.cpp")

add_threadcomponent_http_example("httpClientExample" "httpClientExample.cpp" threadcomponent_http_client)
add_threadcomponent_http_example("httpServerTest" "httpServerTest.cpp" threadcomponent_http_server)
add_threadcomponent_http_example("httpServerTest_V2" "httpServerTest_V2.cpp" threadcomponent_http_server_v2)
add_threadcomponent_https_example("httpsServerTest" "httpsServerTest.cpp" threadcomponent_https_server)
add_threadcomponent_https_example("httpsServerTest_V2" "httpsServerTest_V2.cpp" threadcomponent_https_server_v2)

# Optional gRPC examples: only pulled in when a gRPC toolchain is available so
# machines without gRPC installed can still build the rest of the project.
option(THREADCOMPONENT_BUILD_GRPC_EXAMPLES "Build gRPC example executables" ON)
if(THREADCOMPONENT_BUILD_GRPC_EXAMPLES)
  find_package(gRPC CONFIG QUIET)
  find_package(protobuf CONFIG QUIET)
  if(gRPC_FOUND AND protobuf_FOUND)
    add_subdirectory(gRPC)
  else()
    message(STATUS "gRPC/Protobuf not found - skipping ThreadComponent gRPC examples")
  endif()
endif()
