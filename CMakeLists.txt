cmake_minimum_required(VERSION 3.30)
project(bhas)

option(BHAS_BUILD_TESTS "Build tests" OFF)

find_package(PortAudio REQUIRED CONFIG)

add_library(bhas)
add_library(bhas::bhas ALIAS bhas)

target_sources(bhas PUBLIC
	FILE_SET HEADERS
	BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
	FILES ${CMAKE_CURRENT_LIST_DIR}/include/bhas.h
)

target_sources(bhas PRIVATE
	src/bhas.cpp
	src/bhas_api.h
	src/bhas_api_portaudio.cpp
)

target_include_directories(bhas PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(bhas PUBLIC PortAudio::portaudio)
set_target_properties(bhas PROPERTIES CXX_STANDARD 20)

if (BHAS_BUILD_TESTS)
	add_executable(bhas_tests src/bhas_tests.cpp src/doctest.h)
	target_include_directories(bhas_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_CURRENT_LIST_DIR}/include)
	target_link_libraries(bhas_tests PRIVATE bhas)
	set_target_properties(bhas_tests PROPERTIES CXX_STANDARD 20)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS bhas EXPORT bhasTargets FILE_SET HEADERS)
install(EXPORT bhasTargets FILE bhasTargets.cmake NAMESPACE bhas:: DESTINATION lib/cmake/bhas)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/bhasConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bhasConfig.cmake"
    INSTALL_DESTINATION lib/cmake/bhas
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bhasConfig.cmake" DESTINATION lib/cmake/bhas)