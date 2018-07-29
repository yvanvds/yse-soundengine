CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

PROJECT(yse-soundengine)

# Location where cmake first looks for cmake modules.
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


### PORTAUDIO
add_definitions(-DPORTAUDIO_BACKEND)

OPTION(WANT_PORTAUDIO   "Include PortAudio support" ON)


# check for PortAudio
IF(WANT_PORTAUDIO)
        FIND_PACKAGE(Portaudio)
        IF(PORTAUDIO_FOUND)
                SET(YSE_HAVE_PORTAUDIO TRUE)
                SET(STATUS_PORTAUDIO "OK")
        ELSE(PORTAUDIO_FOUND)
                SET(STATUS_PORTAUDIO "not found, please install portaudio19-dev (or similar, version >= 1.9) "
                        "if you require PortAudio support")
        ENDIF(PORTAUDIO_FOUND)
ENDIF(WANT_PORTAUDIO)

############ SNDFILE
add_definitions(-DLIBSOUNDFILE_BACKEND)

# check for libsndfile
PKG_CHECK_MODULES(SNDFILE REQUIRED sndfile>=1.0.11)
IF(NOT SNDFILE_FOUND)
        MESSAGE(FATAL_ERROR "LMMS requires libsndfile1 and libsndfile1-dev >= 1.0.11 - please install, remove CMakeCache.txt and try again!")
ENDIF(NOT SNDFILE_FOUND)


include_directories("${PROJECT_SOURCE_DIR}/YseEngine")


ADD_SUBDIRECTORY(YseEngine)
ADD_SUBDIRECTORY(Demo.Windows.Native)

