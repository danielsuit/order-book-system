cmake_minimum_required(VERSION 3.16)
project(convergence)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# --- Julia integration for quantum computing ---
option(ENABLE_QUANTUM "Enable quantum computing via Julia/Yao.jl" ON)

if(ENABLE_QUANTUM)
    # Find Julia installation
    # Check JULIA_DIR env var first, then standard locations
    if(DEFINED ENV{JULIA_DIR})
        set(JULIA_DIR $ENV{JULIA_DIR})
    else()
        # Try to find julia executable and derive paths
        find_program(JULIA_EXECUTABLE julia)
        if(JULIA_EXECUTABLE)
            execute_process(
                COMMAND ${JULIA_EXECUTABLE} -e "print(dirname(Sys.BINDIR))"
                OUTPUT_VARIABLE JULIA_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()
    endif()

    if(JULIA_DIR)
        message(STATUS "Julia found at: ${JULIA_DIR}")

        # Julia include and lib directories
        set(JULIA_INCLUDE_DIR "${JULIA_DIR}/include/julia")
        if(NOT EXISTS "${JULIA_INCLUDE_DIR}/julia.h")
            # Try alternative layout
            file(GLOB JULIA_INCLUDE_DIR "${JULIA_DIR}/include/julia*")
        endif()

        find_library(JULIA_LIBRARY
            NAMES julia
            PATHS "${JULIA_DIR}/lib" "${JULIA_DIR}/lib/julia"
        )

        if(EXISTS "${JULIA_INCLUDE_DIR}/julia.h" AND JULIA_LIBRARY)
            message(STATUS "Julia include: ${JULIA_INCLUDE_DIR}")
            message(STATUS "Julia library: ${JULIA_LIBRARY}")
            set(JULIA_FOUND TRUE)
        else()
            message(WARNING "Julia headers or library not found — quantum features disabled")
            set(JULIA_FOUND FALSE)
        endif()
    else()
        message(WARNING "Julia not found — quantum features disabled. Set JULIA_DIR or install Julia.")
        set(JULIA_FOUND FALSE)
    endif()
endif()

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

# Quantum sources (always compiled, but JuliaBridge::initialize() is a no-op without Julia)
set(QUANTUM_SOURCES
    src/quantum/julia_bridge.cpp
    src/quantum/quantum_scheduler.cpp
    src/quantum/quantum_matcher.cpp
)

# Main executable
add_executable(convergence src/main.cpp ${LIB_SOURCES} ${QUANTUM_SOURCES})
target_include_directories(convergence PRIVATE src)
target_link_libraries(convergence PRIVATE pthread)

if(ENABLE_QUANTUM AND JULIA_FOUND)
    target_include_directories(convergence PRIVATE ${JULIA_INCLUDE_DIR})
    target_link_libraries(convergence PRIVATE ${JULIA_LIBRARY})
    target_compile_definitions(convergence PRIVATE CONVERGENCE_QUANTUM_ENABLED=1)
else()
    target_compile_definitions(convergence PRIVATE CONVERGENCE_QUANTUM_ENABLED=0)
endif()

# Copy Julia quantum scripts to build directory
file(COPY src/quantum/julia/ DESTINATION ${CMAKE_BINARY_DIR}/quantum/julia)

# Tests
enable_testing()
add_executable(convergence_tests
    tests/test_orderbook.cpp
    tests/test_vector_clock.cpp
    tests/test_crdt.cpp
    tests/test_message_serialization.cpp
    tests/test_wythoff_scheduler.cpp
    tests/test_quantum.cpp
    tests/test_main.cpp
    ${LIB_SOURCES}
    ${QUANTUM_SOURCES}
)
target_include_directories(convergence_tests PRIVATE src)
target_link_libraries(convergence_tests PRIVATE pthread)

if(ENABLE_QUANTUM AND JULIA_FOUND)
    target_include_directories(convergence_tests PRIVATE ${JULIA_INCLUDE_DIR})
    target_link_libraries(convergence_tests PRIVATE ${JULIA_LIBRARY})
    target_compile_definitions(convergence_tests PRIVATE CONVERGENCE_QUANTUM_ENABLED=1)
else()
    target_compile_definitions(convergence_tests PRIVATE CONVERGENCE_QUANTUM_ENABLED=0)
endif()

add_test(NAME AllTests COMMAND convergence_tests)
