cmake_minimum_required(VERSION 3.16)
project(moyu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    $<$<C_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall -Wextra -Wno-unused-parameter>
)

# ---------- Lua 5.4 (vendored) ----------
file(GLOB LUA_SRC CONFIGURE_DEPENDS third_party/lua/*.c)
add_library(lua_static STATIC ${LUA_SRC})
target_include_directories(lua_static PUBLIC third_party/lua)
if(MSVC)
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------- cJSON (vendored) ----------
add_library(cjson_static STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson_static PUBLIC third_party/cjson)
if(MSVC)
    target_compile_definitions(cjson_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------- MOYU ----------
file(GLOB_RECURSE MOYU_SRC CONFIGURE_DEPENDS
    src/main.c
    src/util/*.c
    src/platform/*.c
    src/render/*.c
    src/core/*.c
    src/llm/*.c
    src/tools/*.c
    src/script/*.c
)

add_executable(moyu WIN32 ${MOYU_SRC})

if(WIN32)
    # .m files excluded by GLOB above; select proper platform source
    set_property(SOURCE src/platform/platform_macos.m PROPERTY HEADER_FILE_ONLY ON)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        list(REMOVE_ITEM MOYU_SRC src/platform/platform_linux.c)
    endif()
endif()

target_include_directories(moyu PRIVATE
    src
    src/util
    src/platform
    src/render
    src/core
    src/llm
    src/tools
    src/script
)

target_link_libraries(moyu PRIVATE lua_static cjson_static)

if(WIN32)
    target_link_libraries(moyu PRIVATE
        winhttp
        user32
        gdi32
        shell32
        ws2_32
    )
endif()

if(APPLE)
    find_library(APPKIT AppKit REQUIRED)
    find_library(FOUNDATION Foundation REQUIRED)
    target_link_libraries(moyu PRIVATE ${APPKIT} ${FOUNDATION})
    set_target_properties(moyu PROPERTIES LINK_FLAGS "-ObjC")
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(X11 QUIET x11)
        pkg_check_modules(CURL QUIET libcurl)
        if(X11_FOUND)
            target_link_libraries(moyu PRIVATE ${X11_LIBRARIES})
            target_include_directories(moyu PRIVATE ${X11_INCLUDE_DIRS})
        endif()
        if(CURL_FOUND)
            target_link_libraries(moyu PRIVATE ${CURL_LIBRARIES})
            target_include_directories(moyu PRIVATE ${CURL_INCLUDE_DIRS})
        endif()
    endif()
endif()

# Copy runtime assets next to the executable
add_custom_command(TARGET moyu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:moyu>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:moyu>/scripts
)
