cmake_minimum_required (VERSION 2.6)
project (gnuais)

INCLUDE(CheckIncludeFiles)

CHECK_INCLUDE_FILES(alsa/asoundlib.h HAVE_ALSA)
CHECK_INCLUDE_FILES(curl/curl.h HAVE_CURL)

set(CMAKE_C_FLAGS "-g -Wall")

if(NOT HAVE_ALSA)
	MESSAGE(FATAL_ERROR "The development files for ALSA are required - libasound-dev")
endif(NOT HAVE_ALSA)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)


add_subdirectory(src)
