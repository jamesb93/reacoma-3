cmake_minimum_required(VERSION 3.28)

project(reaper_flucoma
  VERSION 1.0.1
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

set(SOURCES ./reaper_noveltyslice.cpp)
set(LIBS "")

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architecture" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11" CACHE STRING "Minimum OS X deployment version" FORCE)
    add_definitions(-DNOMINMAX)
endif ()

if (WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(LIBS user32.lib)
endif ()

if (LINUX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_definitions(-DNOMINMAX)
endif ()

include(FetchContent)

FetchContent_Declare(
  reaper_sdk
  GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk.git
  GIT_PROGRESS TRUE
  GIT_TAG        f48a724fb523f6a2f8aeaa4b2023280c3b488f65
)

FetchContent_Declare(
  WDL
  GIT_REPOSITORY https://github.com/justinfrankel/WDL.git
  GIT_PROGRESS TRUE
  GIT_TAG        12c86f9061c0c517bb768c2593eb36670f34eb4d
)

FetchContent_Declare(
  flucoma_core
  GIT_REPOSITORY https://github.com/flucoma/flucoma-core.git
  GIT_PROGRESS TRUE
  GIT_TAG        aa4f5601765bbe08a44fd6118becae701e875121
)

FetchContent_MakeAvailable(reaper_sdk WDL flucoma_core)

file(CREATE_LINK "${CMAKE_BINARY_DIR}/_deps/wdl-src/WDL" "${CMAKE_BINARY_DIR}/_deps/reaper_sdk-src/WDL" SYMBOLIC)

add_library(flucoma SHARED ${SOURCES})

target_include_directories(flucoma PRIVATE 
    ${reaper_sdk_SOURCE_DIR}
    ${reaper_sdk_SOURCE_DIR}/sdk
    ${WDL_SOURCE_DIR}/WDL
    ${flucoma_core_SOURCE_DIR}/include
)

target_link_libraries(flucoma PRIVATE ${LIBS} FLUID_DECOMPOSITION)

set(SIMD_OPT -mavx)

if (APPLE)
    set_target_properties(flucoma PROPERTIES SUFFIX ".dylib")
endif ()

if (WIN32)
    if ("${CMAKE_GENERATOR_PLATFORM}" MATCHES "Win32")
        set_target_properties(flucoma PROPERTIES SUFFIX "_w32.dll")
    else ()
        set_target_properties(flucoma PROPERTIES SUFFIX "_x64.dll")
    endif ()
endif ()

set_target_properties(flucoma PROPERTIES PREFIX "")
set_target_properties(flucoma PROPERTIES OUTPUT_NAME "reaper_flucoma")

target_compile_definitions(flucoma PRIVATE REAPER_PLUGIN)

if(APPLE)
    set(REAPER_USER_PLUGINS_DIR "$ENV{HOME}/Library/Application Support/REAPER/UserPlugins")
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:flucoma>
            "${REAPER_USER_PLUGINS_DIR}"
        COMMENT "Copying flucoma dylib to ${REAPER_USER_PLUGINS_DIR}"
    )
    add_dependencies(deploy flucoma)
endif()