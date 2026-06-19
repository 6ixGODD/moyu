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

# ---------- MOYU (flat src/ layout) ----------
file(GLOB MOYU_SRC CONFIGURE_DEPENDS src/*.c)

# Exclude platform sources that don't apply to the current target.
if(WIN32)
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_linux\\.c$")
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_macos\\.m$")
elseif(APPLE)
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_linux\\.c$")
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_win32\\.c$")
    enable_language(OBJC)
    set(MOYU_OBJC src/platform_macos.m)
else()
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_win32\\.c$")
    list(FILTER MOYU_SRC EXCLUDE REGEX "platform_macos\\.m$")
endif()

if(APPLE)
    add_executable(moyu WIN32 ${MOYU_SRC} ${MOYU_OBJC})
    set_target_properties(moyu PROPERTIES LINK_FLAGS "-ObjC")
else()
    add_executable(moyu WIN32 ${MOYU_SRC})
endif()

target_include_directories(moyu PRIVATE src)

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
