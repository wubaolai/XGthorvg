cmake_minimum_required(VERSION 3.22)
project(ThorVGLottieEditor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()
include(CTest)
option(LOTTIE_DOCUMENT_ONLY "Build JSON document tests without ThorVG or OpenGL" OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
if(APPLE)
    # Some Qt SDKs export only RelWithDebInfo and omit their build RPATH.
    get_target_property(_qt_core Qt6::Core IMPORTED_LOCATION_RELWITHDEBINFO)
    if(NOT _qt_core)
        get_target_property(_qt_core Qt6::Core IMPORTED_LOCATION_RELEASE)
    endif()
    if(_qt_core)
        get_filename_component(_qt_lib "${_qt_core}/../../../.." ABSOLUTE)
        list(APPEND CMAKE_BUILD_RPATH "${_qt_lib}")
    endif()
endif()
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

add_library(lottie_document STATIC src/lottiedocument.cpp src/lottiedocument.h)
target_include_directories(lottie_document PUBLIC src)
target_link_libraries(lottie_document PUBLIC Qt6::Core Qt6::Gui)
if(BUILD_TESTING)
    add_executable(document_test tests/document_test.cpp)
    target_link_libraries(document_test PRIVATE lottie_document Qt6::Test)
    add_test(NAME document COMMAND document_test)
    set_tests_properties(document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
if(LOTTIE_DOCUMENT_ONLY)
    return()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(Threads REQUIRED)
find_program(MESON_EXECUTABLE NAMES meson
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv-demo/bin"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv-demo/Scripts" REQUIRED)
find_program(NINJA_EXECUTABLE NAMES ninja
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv-demo/bin"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../.venv-demo/Scripts" REQUIRED)
get_filename_component(THORVG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(THORVG_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/thorvg")
if(MSVC)
    set(THORVG_LIBRARY "${THORVG_BINARY_DIR}/src/thorvg-1.lib")
else()
    set(THORVG_LIBRARY "${THORVG_BINARY_DIR}/src/libthorvg-1.a")
endif()
include(ExternalProject)
set(THORVG_BUILD_JOBS 4 CACHE STRING "Parallel compiler jobs for the ThorVG dependency")
ExternalProject_Add(thorvg_build
    SOURCE_DIR "${THORVG_SOURCE_DIR}"
    BINARY_DIR "${THORVG_BINARY_DIR}"
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "NINJA=${NINJA_EXECUTABLE}"
        "CXX=${CMAKE_CXX_COMPILER}"
        "${MESON_EXECUTABLE}" setup <BINARY_DIR> <SOURCE_DIR>
        --buildtype=release --backend=ninja
        -Ddefault_library=static -Dstatic=true -Dengines=gl
        -Dloaders=svg,lottie,ttf,otf,png,jpg,webp -Dextra=lottie_exp
        -Dtools= -Dtests=false -Dbindings= -Dlog=false
    BUILD_COMMAND "${CMAKE_COMMAND}" -E env "NINJA=${NINJA_EXECUTABLE}"
        "${NINJA_EXECUTABLE}" -C <BINARY_DIR> -j "${THORVG_BUILD_JOBS}"
    BUILD_BYPRODUCTS "${THORVG_LIBRARY}"
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
)
add_library(thorvg STATIC IMPORTED GLOBAL)
set_target_properties(thorvg PROPERTIES IMPORTED_LOCATION "${THORVG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${THORVG_SOURCE_DIR}/inc"
    INTERFACE_COMPILE_DEFINITIONS TVG_STATIC)
add_dependencies(thorvg thorvg_build)

add_library(lottie_player STATIC src/lottiewidget.cpp src/lottiewidget.h)
target_link_libraries(lottie_player PUBLIC lottie_document Qt6::Widgets
    Qt6::OpenGLWidgets Qt6::OpenGL PRIVATE thorvg Threads::Threads ${CMAKE_DL_LIBS})

add_library(lottie_window STATIC src/mainwindow.cpp src/mainwindow.h)
target_link_libraries(lottie_window PUBLIC lottie_player)
qt_add_executable(qt_lottie_editor src/main.cpp resources.qrc)
target_link_libraries(qt_lottie_editor PRIVATE lottie_window)
set_target_properties(qt_lottie_editor PROPERTIES
    MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER "org.thorvg.qt-lottie-demo"
    WIN32_EXECUTABLE TRUE)
if(BUILD_TESTING)
    add_executable(gpu_test tests/gpu_test.cpp resources.qrc)
    target_link_libraries(gpu_test PRIVATE lottie_player Qt6::Test)
    add_test(NAME gpu COMMAND gpu_test)
    set_tests_properties(gpu PROPERTIES LABELS "gpu" TIMEOUT 60)
    add_executable(ui_test tests/ui_test.cpp resources.qrc)
    target_link_libraries(ui_test PRIVATE lottie_window Qt6::Test)
    add_test(NAME ui COMMAND ui_test)
    set_tests_properties(ui PROPERTIES LABELS "gpu" TIMEOUT 60)
endif()
