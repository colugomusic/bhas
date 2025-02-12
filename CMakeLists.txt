cmake_minimum_required(VERSION 3.12)
project(bhas)

set(debug_build $<OR:$<CONFIG:Debug>,$<STREQUAL:${CMAKE_BUILD_TYPE},Debug}>>)

# ASIO SDK ######################################################
FetchContent_Declare(asio_sdk URL https://www.steinberg.net/asiosdk)
FetchContent_MakeAvailable(asio_sdk)
# This is used later by the portaudio CMakeLists
set(ASIOSDK_PATH_HINT ${asio_sdk_SOURCE_DIR})
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# portaudio #####################################################
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
	set(PA_USE_ALSA ON CACHE BOOL "Enable support for ALSA")
	set(PA_USE_JACK ON CACHE BOOL "Enable support for Jack")
endif()
set(PA_USE_ASIO TRUE CACHE BOOL "Enable support for ASIO")
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(extern/submodules/portaudio)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# library #######################################################
add_library(bhas STATIC
	include/bhas.h
	src/bhas.cpp
	src/bhas_api.h
	src/bhas_api_portaudio.cpp
)
add_library(bhas::bhas ALIAS bhas)
target_include_directories(bhas PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/src
)
target_include_directories(bhas PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(bhas PUBLIC
	portaudio
)
set_target_properties(bhas PROPERTIES
	CXX_STANDARD 20
)
target_compile_definitions(bhas PRIVATE
	$<${debug_build}:_DEBUG>
)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
# tests #########################################################
add_executable(bhas_tests
	src/bhas_tests.cpp
	src/doctest.h
)
target_include_directories(bhas_tests PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/src
	${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(bhas_tests PRIVATE
	bhas
)
set_target_properties(bhas_tests PROPERTIES
	CXX_STANDARD 20
)
target_compile_definitions(bhas_tests PRIVATE
	$<${debug_build}:_DEBUG>
)
#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
