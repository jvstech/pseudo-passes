cmake_minimum_required(VERSION 3.12)

project(PseudoPasses)

if (DEFINED PATH_TO_LLVM AND NOT "${PATH_TO_LLVM}" STREQUAL "")
  set(llvm_search_paths
    ${PATH_TO_LLVM}
    ${PATH_TO_LLVM}/lib
    ${PATH_TO_LLVM}/lib/cmake
    ${PATH_TO_LLVM}/lib/cmake/llvm
    ${PATH_TO_LLVM}/share/llvm/cmake
    )

  find_package(LLVM REQUIRED CONFIG
    PATHS ${llvm_search_paths}
    NO_DEFAULT_PATH)
else()
  find_package(LLVM REQUIRED CONFIG)
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_INSTALL_PREFIX}")
set(CMAKE_MODULE_PATH 
  "${CMAKE_MODULE_PATH};${LLVM_INSTALL_PREFIX}/lib/cmake/llvm")
set(CMAKE_MODULE_PATH 
  "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/cmake")
include(AddLLVM)
include(AddPortableLLVMPlugin)

set(LLVM_ENABLE_PLUGINS ON)
if (WIN32 OR CYGWIN)
  set(LLVM_EXPORT_SYMBOLS_FOR_PLUGINS ON)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (NOT LLVM_ENABLE_RTTI)
  if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
  endif()
endif()
include_directories(${LLVM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
add_definitions(${LLVM_DEFINITIONS})

if (WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS Core Support Passes TransformUtils)
endif()

if (APPLE)
  link_libraries("$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")
endif()

add_subdirectory(lib)
add_subdirectory(benchmarks)

