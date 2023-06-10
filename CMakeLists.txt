cmake_minimum_required(VERSION 3.24)

project(rps CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SRC ${PROJECT_SOURCE_DIR}/src/main.cpp)
set(RES ${PROJECT_SOURCE_DIR}/res/main.rc)

source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Source Files" FILES ${SRC})

add_subdirectory(${PROJECT_SOURCE_DIR}/lib/fmt)
include_directories(${PROJECT_SOURCE_DIR}/res)

add_executable(rps ${SRC})
target_sources(rps PRIVATE ${RES})
target_link_libraries(rps fmt-header-only)
