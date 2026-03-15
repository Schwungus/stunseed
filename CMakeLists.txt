cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(stunseed VERSION 0.1.0
    LANGUAGES C CXX)

option(STUNSEED_BUILD_TEST "Build a stunseed test binary?" OFF)

# static-library definition

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF) # libjuice won't build otherwise, damn it

if(CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c*)
file(GLOB_RECURSE HEADERS ${INCLUDE_DIR}/*.h)

set(CONFIGURE_FILE ${CMAKE_CURRENT_BINARY_DIR}/stunseed/stunseed-cmake.h)
configure_file(${INCLUDE_DIR}/stunseed-cmake.h.in ${CONFIGURE_FILE} @ONLY)
list(APPEND HEADERS ${CONFIGURE_FILE})

add_library(stunseed ${SOURCES} ${HEADERS})
target_include_directories(stunseed
    PUBLIC ${INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/stunseed)
set_target_properties(stunseed PROPERTIES LINKER_LANGUAGE CXX)

# external deps

## S_tructures for hashmaps

FetchContent_Declare(S_tructures
    GIT_REPOSITORY https://github.com/Schwungus/S_tructures.git
    GIT_TAG master) # TODO: pin
list(APPEND LIBS S_tructures)

## bq_websocket lets us abuse webtorrent trackers for FREE signalling

FetchContent_Declare(bq_websocket
    GIT_REPOSITORY https://github.com/Schwungus/bq_websocket.git
    GIT_TAG master) # TODO: pin
list(APPEND LIBS bq_websocket)

## yyjson for forging packets to achieve the aforementioned FREE signalling

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0)
list(APPEND LIBS yyjson)

## libdatachannel for webrtc

set(NO_MEDIA ON CACHE INTERNAL "")
set(NO_EXAMPLES ON CACHE INTERNAL "")
set(NO_TESTS ON CACHE INTERNAL "")

if(CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    FetchContent_Declare(datachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/datachannel-wasm.git
        GIT_TAG v0.4.0)
    list(APPEND LIBS datachannel-wasm)

    target_link_options(stunseed PUBLIC -s ASYNCIFY)
    target_compile_options(stunseed PUBLIC -O3) # the emscripten manual recommends this
else()
    FetchContent_Declare(datachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.24.1)
    list(APPEND LIBS datachannel-static)
endif()

## actually linking against those libs

FetchContent_MakeAvailable(S_tructures datachannel yyjson bq_websocket)
target_link_libraries(stunseed PRIVATE ${LIBS})

# test-binary definition

if(STUNSEED_BUILD_TEST)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5)
    FetchContent_MakeAvailable(raylib)

    add_executable(stunseed-test ${CMAKE_CURRENT_SOURCE_DIR}/stunseed-test.c)
    target_link_libraries(stunseed-test PRIVATE stunseed raylib)
endif()
