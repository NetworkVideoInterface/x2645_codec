cmake_minimum_required(VERSION 3.20)

set_property(GLOBAL PROPERTY USE_FOLDERS On)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

#rpath
if(UNIX)
    if(APPLE)
        set(CMAKE_MACOSX_RPATH ON)
    else()
        set(CMAKE_INSTALL_RPATH "$ORIGIN")
    endif()
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
endif()

# some compatibility
if(CMAKE_VERSION VERSION_LESS 3.25)
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(LINUX TRUE)
    endif()
endif()

project(X2645Plugin)

if (WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if (APPLE)
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
endif()

option(MSVC_STATIC_RT "Use Static VC Run-Time Library." ON)
if(MSVC)
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    if (MSVC_STATIC_RT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Zc:__cplusplus>)
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/std:c++17>)
    else()
        add_compile_options("/Zc:__cplusplus" "/std:c++17")
    endif()
endif(MSVC)

if (CMAKE_COMPILER_IS_GNUCXX)
    add_link_options("-Wl,--exclude-libs,ALL")
endif()

set(CMAKE_CXX_STANDARD 17) #-stdc++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) #-fPIC
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_OSX_ARCHITECTURES})
endif()
if (NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

set(LIB_VERSION "" CACHE STRING "The library version.")
set(LIB_PUBLISHER "" CACHE STRING "The library publisher name.")
set(LIB_COPYRIGHT "" CACHE STRING "The library copyright holder name.")
set(NVI_PATH "" CACHE STRING "The NVI repository or include path.")

if (ANDROID)
    # Fix https://github.com/android/ndk/issues/912
    list(APPEND CMAKE_FIND_ROOT_PATH ${PROJECT_SOURCE_DIR})
endif()
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/depends)

find_path(NVI_INCLUDE_DIR "NVI/Codec.h" HINTS ${NVI_PATH} ${NVI_PATH}/src/interface)
if (NVI_INCLUDE_DIR)
    message(STATUS "NVI interface: ${NVI_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "Not found NVI interface.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/script/cmake/find_x264.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/script/cmake/find_x265.cmake)
find_package(fmt CONFIG QUIET)
file(GLOB_RECURSE SRC_FILES "src/*.h" "src/*.hpp" "src/*.cpp")
add_library(${PROJECT_NAME} SHARED ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${NVI_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE x264)
target_compile_definitions(${PROJECT_NAME} PRIVATE LIBX264_VERSION="${x264_VERSION}")
target_compile_definitions(${PROJECT_NAME} PRIVATE NVI_EXPORTS)
if (TARGET x265)
    target_link_libraries(${PROJECT_NAME} PRIVATE x265)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_X265 LIBX265_VERSION="${x265_VERSION}")
endif()
if (TARGET fmt::fmt-header-only)
    target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt-header-only)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _HAS_FMT)
endif()

if (LIB_VERSION)
    message(STATUS "The X2645Plugin library version is ${LIB_VERSION}")
    string(REPLACE "." ";" LIB_VERSION_SEMANTIC "${LIB_VERSION}")
    list(GET LIB_VERSION_SEMANTIC 0 LIB_VERSION_MAJOR)
    list(GET LIB_VERSION_SEMANTIC 1 LIB_VERSION_MINOR)
    list(GET LIB_VERSION_SEMANTIC 2 LIB_VERSION_PATCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        LIB_VERSION=${LIB_VERSION}
        LIB_VERSION_MAJOR=${LIB_VERSION_MAJOR}
        LIB_VERSION_MINOR=${LIB_VERSION_MINOR}
        LIB_VERSION_PATCH=${LIB_VERSION_PATCH}
    )
    if (WIN32)
        string(TIMESTAMP BUILD_TIME_YEAR "%Y")
        string(TIMESTAMP BUILD_TIME_MONTH "%m")
        string(TIMESTAMP BUILD_TIME_DAY "%d")
        string(TIMESTAMP BUILD_TIME_TIME "%H%M")
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
        target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH} SOVERSION ${LIB_VERSION_MAJOR})
    endif()
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_PREFIX})
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION lib
)
if (APPLE)
    install(FILES $<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:${PROJECT_NAME}>.dSYM DESTINATION dsym)
endif()
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION pdb OPTIONAL)
endif()

