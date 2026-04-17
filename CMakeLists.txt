cmake_minimum_required(VERSION 3.16)
project(capture_library)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common sources
set(COMMON_SOURCES
    src/common/error.cpp
    src/common/monotonic_time.cpp
    src/common/png_writer.cpp
    src/common/factory.cpp
)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SOURCES
        src/windows/monitor_enum.cpp
        src/windows/d3d11_helpers.cpp
        src/windows/wgc_interop.cpp
        src/windows/backend_windows_wgc.cpp
    )
    set(PLATFORM_LIBS d3d11 dxgi windowsapp)
else()
    set(PLATFORM_SOURCES
        src/linux/backend_linux_portal_pipewire.cpp
    )
    set(PLATFORM_LIBS)
endif()

# Library
add_library(capture_lib
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(capture_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_lib
    PRIVATE
        ${PLATFORM_LIBS}
)

# Example executable: single capture
add_executable(capture_once
    examples/capture_once.cpp
)

target_include_directories(capture_once
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(capture_once
    PRIVATE
        capture_lib
)

set_target_properties(capture_once PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Example executable: benchmarking
add_executable(benchmark_fps
    examples/benchmark_fps.cpp
)

target_include_directories(benchmark_fps
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(benchmark_fps
    PRIVATE
        capture_lib
)

set_target_properties(benchmark_fps PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Set startup project on Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT capture_once)
endif()
