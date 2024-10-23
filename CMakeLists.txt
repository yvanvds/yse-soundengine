CMAKE_MINIMUM_REQUIRED(VERSION 3.11)

PROJECT(yse-soundengine)

# Location where cmake first looks for cmake modules.
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

### PORTAUDIO
add_definitions(-DPORTAUDIO_BACKEND)

OPTION(WANT_PORTAUDIO   "Include PortAudio support" ON)

FetchContent_Declare(
    Portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG master
    GIT_PROGRESS ON
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS NAMES portaudio
)
FetchContent_MakeAvailable(Portaudio)

FetchContent_Declare(
    sndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG master
    GIT_PROGRESS ON
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS NAMES libsndfile
)
FetchContent_MakeAvailable(sndfile)

add_definitions(-DLIBSOUNDFILE_BACKEND)



include_directories("${PROJECT_SOURCE_DIR}/YseEngine")


ADD_SUBDIRECTORY(YseEngine)
ADD_SUBDIRECTORY(Demo.Windows.Native)

