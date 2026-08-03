# FalconOS v2.1 "Nexus" - Build System

CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
project(FalconOS VERSION 2.1.0 LANGUAGES C Rust ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUST_STANDARD 2021)

# Compiler flags for optimization
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -mtune=native")
set(CMAKE_RUST_FLAGS_RELEASE "-C opt-level=3 -C target-cpu=native")

# Architecture detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH x86_64)
    add_definitions(-DARCH_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(ARCH aarch64)
    add_definitions(-DARCH_AARCH64)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/kernel)

# Kernel sources
set(KERNEL_SOURCES
    src/kernel/boot.c
    src/kernel/wine_bridge.c
    src/kernel/gui_server.c
)

# Create kernel executable
add_executable(falcon_kernel ${KERNEL_SOURCES})

# Link Rust kernel entry point
target_link_libraries(falcon_kernel PRIVATE kernel_main)

# Add Rust library
add_library(kernel_main STATIC src/kernel/main.rs)
set_target_properties(kernel_main PROPERTIES
    LINKER_LANGUAGE C
)

# Installation rules
install(TARGETS falcon_kernel DESTINATION boot)
install(DIRECTORY include/ DESTINATION include)

# Custom targets for building ISO
add_custom_target(iso
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_iso.sh
    DEPENDS falcon_kernel
    COMMENT "Building FalconOS ISO image"
)

# Custom targets for testing
add_custom_target(test
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_tests.sh
    DEPENDS falcon_kernel
    COMMENT "Running FalconOS tests"
)

# Print build configuration
message(STATUS "FalconOS v2.1 Nexus Build Configuration:")
message(STATUS "  Architecture: ${ARCH}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  Rust Compiler: ${CMAKE_RUST_COMPILER}")
