cmake_minimum_required(VERSION 3.16)
project(moyu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

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

# ---------- SQLite (official amalgamation, public domain) ----------
add_library(sqlite_static STATIC third_party/sqlite/sqlite3.c)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sqlite_static PRIVATE -Wno-unused-function -Wno-unused-variable)
endif()
target_include_directories(sqlite_static PUBLIC third_party/sqlite)
target_compile_definitions(sqlite_static PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
)
if(MSVC)
    target_compile_definitions(sqlite_static PRIVATE _CRT_SECURE_NO_WARNINGS)
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

target_include_directories(moyu PRIVATE third_party/sqlite)
target_link_libraries(moyu PRIVATE lua_static cjson_static sqlite_static)

if(WIN32)
    target_link_libraries(moyu PRIVATE
        winhttp
        user32
        gdi32
        shell32
        ole32
        crypt32
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

if(WIN32)
    add_executable(moyu_chat tools/openchat.c
        src/hash.c src/log.c src/mem.c src/platform_win32.c src/workdir.c
        src/state.c src/memory.c src/secrets.c src/tool.c src/mcp.c src/llm.c)
    set_target_properties(moyu_chat PROPERTIES OUTPUT_NAME "moyu-chat")
    target_include_directories(moyu_chat PRIVATE src third_party/cjson third_party/sqlite)
    target_compile_definitions(moyu_chat PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(moyu_chat PRIVATE cjson_static sqlite_static winhttp user32 gdi32 shell32 crypt32 ws2_32)

    foreach(kind IN ITEMS git notes weather)
        if(kind STREQUAL "git")
            set(kind_id 1)
        elseif(kind STREQUAL "notes")
            set(kind_id 2)
        else()
            set(kind_id 3)
        endif()
        add_executable(moyu_mcp_${kind} examples/mcp/reference_server.c)
        target_compile_definitions(moyu_mcp_${kind} PRIVATE SERVER_KIND=${kind_id} _CRT_SECURE_NO_WARNINGS)
        target_include_directories(moyu_mcp_${kind} PRIVATE third_party/cjson)
        target_link_libraries(moyu_mcp_${kind} PRIVATE cjson_static winhttp ws2_32)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(moyu_mcp_${kind} PRIVATE -Wno-unused-function -Wno-unused-variable)
        endif()
    endforeach()

    add_executable(moyu_tests
        tests/test_main.c
        src/hash.c src/log.c src/mem.c src/platform_win32.c
        src/workdir.c src/state.c src/memory.c src/tool.c src/mcp.c)
    target_sources(moyu_tests PRIVATE src/procedural.c src/sprite.c src/image.c)
    target_include_directories(moyu_tests PRIVATE src third_party/cjson third_party/sqlite)
    target_compile_definitions(moyu_tests PRIVATE _CRT_SECURE_NO_WARNINGS SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_MEMSTATUS=0)
    target_link_libraries(moyu_tests PRIVATE cjson_static sqlite_static winhttp user32 gdi32 shell32 ws2_32)
endif()
