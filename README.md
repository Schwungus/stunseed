# stunseed

Don't read backwards! A STUNning **P2P networking solution** for games that is hosting-free and works both **natively** AND **in the browser**.

TODO: explain the project further.

## Installation

If you're using CMake (as you should), add the following boilerplate to your `CMakeLists.txt` in order to build stunseed from source and include it in your project:

```cmake
include(FetchContent)
FetchContent_Declare(stunseed
    GIT_REPOSITORY https://github.com/Schwungus/stunseed.git
    GIT_TAG master) # TODO: replace with a version tag
FetchContent_MakeAvailable(stunseed)
target_link_libraries(MyEpicGame PRIVATE stunseed)
```

If you're using a different build system, you're probably on your own. Submit a PR if you need it supported!

## Usage

TODO: explain usage once there is any.
