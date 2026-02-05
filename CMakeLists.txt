cmake_minimum_required(VERSION 3.20)
project(libbbf VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Catch2 3)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
if( NOT CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE Debug)
endif()

add_library(libbbf
    src/libbbf.h 
    src/vend/xxhash.c
    src/bbfcodec.cpp
    src/muxer/dedupemap.cpp
    src/muxer/stringpool.cpp
    )

add_library(libbbf_shared SHARED
    src/libbbf.h 
    src/vend/xxhash.c
    src/bbfcodec.cpp
    src/muxer/dedupemap.cpp
    src/muxer/stringpool.cpp
)

target_include_directories(libbbf PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vend"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/muxer"
)

target_include_directories(libbbf_shared PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vend"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/muxer"
)

target_compile_definitions(libbbf_shared PRIVATE LIBBBF_EXPORT_SYMBOLS)

set_target_properties(libbbf PROPERTIES PUBLIC_HEADER src/libbbf.h)
set_target_properties(libbbf PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libbbf PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(libbbf_shared PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(libbbf_shared PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS libbbf libbbf_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

add_executable(bbfmux src/muxer/bbfmux.cpp)

target_link_libraries(bbfmux PRIVATE libbbf)

install(TARGETS bbfmux DESTINATION bin)

if ( Catch2_FOUND )

    add_executable(bbfbench src/bench/bbfbench.cpp src/bench/miniz.c)
    target_link_libraries(bbfbench PRIVATE libbbf Catch2::Catch2WithMain)

    enable_testing()
    include( Catch )
    catch_discover_tests( bbfbench )

else()
    message(WARNING "Catch2 not found! Not building bbfbench.")
endif()

if(MSVC)
    target_compile_options(bbfmux PRIVATE /W4 /O2)
else()
    target_compile_options(bbfmux PRIVATE -g -Wall -Wextra -Wshadow -Wpedantic -std=gnu++17 -Werror)
endif()

if(EMSCRIPTEN)

    message(STATUS "Configuring for WebAssembly (Emscripten)...")

    add_executable(libbbf_wasm 
        src/libbbf.h 
        src/vend/xxhash.c
        src/bbfcodec.cpp
        src/muxer/dedupemap.cpp
        src/muxer/stringpool.cpp
        src/bind/bbfwasm.cpp
    )

    target_include_directories(libbbf_wasm PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vend"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/muxer"
    )

    target_compile_definitions(libbbf_wasm PRIVATE 
        LIBBBF_EXPORT_SYMBOLS 
        __EMSCRIPTEN__
    )

    target_link_options(libbbf_wasm PUBLIC
        "-s WASM=1"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s FILESYSTEM=1"
        "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
        "-s MODULARIZE=1"
        "-s EXPORT_NAME='libbbfAPI'"
    )

    set(CMAKE_EXECUTABLE_SUFFIX ".js")
endif()
