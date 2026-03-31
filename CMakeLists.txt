cmake_minimum_required(VERSION 3.16)
project(convergence)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# --- Source files ---
set(LIB_SOURCES
    src/orderbook/orderbook.cpp
    src/crdt/crdt_orderbook.cpp
    src/crdt/vector_clock.cpp
    src/gossip/gossip_engine.cpp
    src/gossip/message.cpp
    src/gossip/peer.cpp
    src/gossip/wythoff_scheduler.cpp
    src/network/udp_transport.cpp
    src/network/tcp_server.cpp
    src/api/client_handler.cpp
    src/util/config.cpp
    src/util/logger.cpp
    src/util/uuid.cpp
)

# Main executable
add_executable(convergence src/main.cpp ${LIB_SOURCES})
target_include_directories(convergence PRIVATE src)
target_link_libraries(convergence PRIVATE pthread)

# Tests
enable_testing()
add_executable(convergence_tests
    tests/test_orderbook.cpp
    tests/test_vector_clock.cpp
    tests/test_crdt.cpp
    tests/test_message_serialization.cpp
    tests/test_wythoff_scheduler.cpp
    tests/test_main.cpp
    ${LIB_SOURCES}
)
target_include_directories(convergence_tests PRIVATE src)
target_link_libraries(convergence_tests PRIVATE pthread)

add_test(NAME AllTests COMMAND convergence_tests)
