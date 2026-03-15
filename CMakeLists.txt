cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(stunseed VERSION 0.1.0
    LANGUAGES C CXX)

option(STUNSEED_BUILD_TEST "Build a stunseed test binary?" OFF)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF) # libjuice won't build otherwise damn it

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
file(GLOB_RECURSE HEADERS ${INCLUDE_DIR}/*.h)

set(CONFIGURE_FILE ${CMAKE_CURRENT_BINARY_DIR}/stunseed/stunseed-cmake.h)
configure_file(${INCLUDE_DIR}/stunseed-cmake.h.in ${CONFIGURE_FILE} @ONLY)
list(APPEND HEADERS ${CONFIGURE_FILE})

add_library(stunseed ${SOURCES} ${HEADERS})
target_include_directories(stunseed
    PUBLIC ${INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/stunseed)
set_target_properties(stunseed PROPERTIES LINKER_LANGUAGE CXX)

set(NO_MEDIA ON)
set(NO_EXAMPLES ON)
set(NO_TESTS ON)

if(CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    FetchContent_Declare(datachannel-wasm
        GIT_REPOSITORY https://github.com/paullouisageneau/datachannel-wasm.git
        GIT_TAG v0.4.0)
    FetchContent_MakeAvailable(datachannel-wasm)
    target_link_libraries(stunseed PRIVATE datachannel-wasm)
else()
    FetchContent_Declare(datachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.24.1)
    FetchContent_MakeAvailable(datachannel)
    target_link_libraries(stunseed PRIVATE datachannel-static)
endif()

if(STUNSEED_BUILD_TEST)
    add_executable(stunseed-test ${CMAKE_CURRENT_SOURCE_DIR}/stunseed-test.c)
    target_link_libraries(stunseed-test PRIVATE stunseed)
endif()
