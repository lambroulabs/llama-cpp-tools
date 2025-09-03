cmake_minimum_required(VERSION 3.14)
project(llama-cpp-tools VERSION 0.1.0 LANGUAGES CXX)

cmake_policy(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JSON_Install OFF CACHE INTERNAL "")
find_package(nlohmann_json 3.2.0 REQUIRED)

add_library(llama_cpp_tools SHARED src/tool_registry.cpp)
target_link_libraries(llama_cpp_tools PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(llama_cpp_tools
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(llama_cpp_tools PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "include/llama_cpp_tools/tool_registry.h"
)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  enable_testing()
  add_executable(tests tests/tests.cpp)
  target_link_libraries(tests
    PRIVATE
      llama_cpp_tools
      nlohmann_json::nlohmann_json
      Catch2::Catch2WithMain
  )
  add_test(NAME llama_cpp_tools_tests COMMAND tests)
endif()

# Prefer lib64 on 64-bit RHEL/Fedora
include(GNUInstallDirs)

install(TARGETS llama_cpp_tools
  EXPORT llama_cpp_toolsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # automatically lib64 on x86_64
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llama_cpp_tools
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT llama_cpp_toolsTargets
  FILE llama_cpp_toolsTargets.cmake
  NAMESPACE llama_cpp_tools::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/llama-cpp-tools
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/llama-cpp-tools.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/llama-cpp-tools.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/llama-cpp-tools.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})



set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_PACKAGE_NAME "llama-cpp-tools")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "alexanderlambrou0602@gmail.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "alexanderlambrou0602@gmail.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)