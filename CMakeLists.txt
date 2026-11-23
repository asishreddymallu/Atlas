cmake_minimum_required(VERSION 3.19)
project(Atlas LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# This works for Harshit
# set( CMAKE_PREFIX_PATH "D:/QT/6.9.2/mingw_64/lib/cmake")
# set(MINGW_PATH "D:/Qt/Tools/mingw1310_64")
find_package(Qt6 6.9.2 REQUIRED COMPONENTS Core Widgets Network Concurrent)

# This works for Atharva
# set(CMAKE_PREFIX_PATH "/home/atharv/Qt/6.10.0/gcc_64")
# set(Qt6_DIR "/home/atharv/Qt/6.10.0/gcc_64/lib/cmake/Qt6")
# find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
# tests
option(ENABLE_TESTS "Build tests" OFF)
option(LOG_PANEL "Enable LogPanel mode" OFF) # will enable log_panel
option(DISABLE_GUI "Disable GUI components" OFF) # will disable GUI components
qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(SRC_DIR "src")
set(EXTERNAL_DIR "external")
# TEST_DIR was never set, so the TEST_FILES glob below expanded to the
# pattern "/*.cpp" - an ABSOLUTE path - and both the configure step and
# every CONFIGURE_DEPENDS re-check walked the whole filesystem. See
# docs/audit/known_issues.md 68.
set(TEST_DIR "test")

set(CMAKE_AUTOUIC_SEARCH_PATHS  ${SRC_DIR}/ui)

file(GLOB LUA_SRC_FILES CONFIGURE_DEPENDS "${EXTERNAL_DIR}/lua-5.5.0/src/*.c")
# Exclude the standalone interpreter/compiler entry points.
list(FILTER LUA_SRC_FILES EXCLUDE REGEX ".*[\\/]lua[.]c$")
list(FILTER LUA_SRC_FILES EXCLUDE REGEX ".*[\\/]luac[.]c$")

set_source_files_properties(${LUA_SRC_FILES}
    PROPERTIES
    COMPILE_OPTIONS "-Wno-pedantic"
)


# CONFIGURE_DEPENDS so a new source or header is picked up by a plain
# `cmake --build`, without remembering to re-run cmake. Adding a new
# Q_OBJECT header without it meant the build linked against a moc that was
# never generated, and the error named the missing symbol rather than the
# missing file.
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")
file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.ui")
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
    "${SRC_DIR}/*.h")

# src/scheduler/ is its own Qt-free library (atlas_scheduler, below), so it
# is kept out of atlas_core's globs.
list(FILTER SRC_FILES EXCLUDE REGEX ".*/src/scheduler/.*")
list(FILTER HEADER_FILES EXCLUDE REGEX ".*/src/scheduler/.*")
file(GLOB SCHEDULER_SRC_FILES CONFIGURE_DEPENDS "${SRC_DIR}/scheduler/*.cpp")
file(GLOB SCHEDULER_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/scheduler/*.h")

list(APPEND SRC_FILES ${LUA_SRC_FILES})

##############resourecs setup#################
qt6_add_resources(APP_RESOURCES 
resources/resources.qrc
resources/circuit_designs/circuit_designs.qrc
resources/icons/icons.qrc
resources/fonts/fonts.qrc
resources/themes/themes.qrc
resources/instructions/instructions.qrc
)
##############################################

#setting the exe icon for windows
if(WIN32)
    set(APP_RESOURCES ${APP_RESOURCES} resources/icons/app_icon.rc)
endif()

# The code-reordering scheduler (original author: Nandan Nihal,
# CS25BTECH11014). Qt-free on purpose: it links no Qt and gets no Qt include
# paths, so a Qt include in it fails to compile. It reads the assembler's
# Qt-free headers and calls instruction_set::isValid*() from atlas_core's
# common/instructions.cpp, which every consumer of this library also links.
add_library(atlas_scheduler STATIC
    ${SCHEDULER_HEADER_FILES}
    ${SCHEDULER_SRC_FILES}
)
set_target_properties(atlas_scheduler PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(atlas_scheduler PUBLIC ${SRC_DIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(atlas_scheduler PRIVATE -Wall -Wextra -pedantic)
endif()

add_library(atlas_core STATIC
    ${HEADER_FILES}
    ${SRC_FILES}
    ${UI_FILES}
)

target_include_directories(atlas_core PUBLIC
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}_autogen/include"
    ${SRC_DIR}
    ${EXTERNAL_DIR}/lua-5.5.0/src
)

target_link_libraries(atlas_core
    atlas_scheduler
    Qt::Core
    Qt::Widgets
    Qt::Network
    Qt::Concurrent
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(atlas_core PRIVATE 
        -Wall 
        -Wextra 
        -pedantic 
        -frounding-math 
        -ffloat-store 
    )
endif()

if(LOG_PANEL)
    target_compile_definitions(atlas_core PRIVATE LOG_PANEL)
endif()

if(DISABLE_GUI)
    target_compile_definitions(atlas_core PRIVATE DISABLE_GUI)
endif()

target_precompile_headers(atlas_core PRIVATE
    # <cstdint>
    # <string>
    # <array>
    # <vector>
)

#Main application executable
qt_add_executable(${PROJECT_NAME}
    WIN32 MACOSX_BUNDLE
    main.cpp
    ${APP_RESOURCES}
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Atlas")
target_link_libraries(${PROJECT_NAME} PRIVATE atlas_core)

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Command-line executable (no GUI/QApplication dependency at runtime).
add_executable(atlas_cli cli/main.cpp cli/repl.cpp)
set_target_properties(atlas_cli PROPERTIES OUTPUT_NAME "atlas-cli")
target_link_libraries(atlas_cli PRIVATE atlas_core)
target_compile_definitions(atlas_cli PRIVATE DISABLE_GUI)

install(TARGETS atlas_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


if(ENABLE_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    option (DISABLE_GUI "Disable GUI components in tests" ON)
    option (VM_DEBUG_PRINTS "Enable debug prints in VM during tests" OFF)
    enable_testing()
    add_subdirectory(test)
endif()
