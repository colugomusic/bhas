cmake_minimum_required(VERSION 3.12)
project(bhas)

set(debug_build $<OR:$<CONFIG:Debug>,$<STREQUAL:${CMAKE_BUILD_TYPE},Debug}>>)

# ASIO SDK ######################################################
CPMAddPackage(
    NAME          asio_sdk
    URL           https://www.steinberg.net/asiosdk
    DOWNLOAD_ONLY YES
)
# This is used later by the portaudio CMakeLists
set(ASIOSDK_PATH_HINT ${asio_sdk_SOURCE_DIR})
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# portaudio #####################################################
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
	set(PA_USE_ALSA ON CACHE BOOL "Enable support for ALSA")
	set(PA_USE_JACK ON CACHE BOOL "Enable support for Jack")
endif()
set(PA_USE_ASIO TRUE CACHE BOOL "Enable support for ASIO")
CPMAddPackage(
	NAME           portaudio
	GIT_REPOSITORY https://github.com/colugomusic/portaudio.git
	GIT_TAG        master
	OPTIONS        "BUILD_SHARED_LIBS OFF"
)
list(APPEND bhas_library_include_dirs ${portaudio_SOURCE_DIR}/include)
list(APPEND bhas_library_link_libs PortAudio)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# fmt ###########################################################
CPMAddPackage(
	NAME              fmt
	GITHUB_REPOSITORY "fmtlib/fmt"
	GIT_TAG 10.1.1
)
list(APPEND bhas_library_include_dirs ${fmt_SOURCE_DIR}/include)
list(APPEND bhas_library_link_libs fmt::fmt)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# library #######################################################
add_library(${PROJECT_NAME} STATIC
	src/bhas.cpp
  src/bhas_api.h
	src/bhas_api_portaudio.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${bhas_library_include_dirs}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
	${bhas_library_link_libs}
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	$<${debug_build}:_DEBUG>
)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# tests #########################################################
list(APPEND bhas_tests_src_files
	src/bhas_tests.cpp
)
add_executable(bhas_tests
	src/bhas_tests.cpp
)
target_include_directories(bhas_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(bhas_tests PRIVATE
	bhas
)
set_target_properties(bhas_tests PROPERTIES
    CXX_STANDARD 17
)
target_compile_definitions(bhas_tests PRIVATE
	$<${debug_build}:_DEBUG>
)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
