cmake_minimum_required(VERSION 3.16)
project(cleanmodels-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Version stamped into the Windows executable's version info. CI passes the
# release tag without its "v" (1.3.9); local builds get 0.0.0.
set(CLEANMODELS_QT_VERSION "0.0.0" CACHE STRING "Release version, MAJOR.MINOR.PATCH")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui OpenGL OpenGLWidgets)

add_executable(${PROJECT_NAME}
    main.cpp
    mainwindow.cpp
    mainwindow_clean.cpp
    fsmodel.cpp
    camera.cpp
    mdlscene.cpp
    mdlanimationplayer.cpp
    gpumesh.cpp
    gputexture.cpp
    renderer.cpp
    modelviewport.cpp
    variantsdialog.cpp
    previewdir.h
    constants.h
    icons.qrc
    mainwindow.ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets)

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE TRUE)
endif()
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

    # Icon and version info (cleanmodels-qt_resource.rc.in).
    # The numeric fields take MAJOR.MINOR.PATCH; a suffix such as -rc1 only
    # shows in the version strings.
    if(NOT CLEANMODELS_QT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        message(FATAL_ERROR "CLEANMODELS_QT_VERSION must start with MAJOR.MINOR.PATCH, got '${CLEANMODELS_QT_VERSION}'")
    endif()
    set(CMQT_VERSION_COMMAS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
    set(CMQT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/cmicon.ico")
    configure_file(cleanmodels-qt_resource.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/cleanmodels-qt_resource.rc @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/cleanmodels-qt_resource.rc)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Unit tests (Qt Test): ctest --test-dir build -C Release
option(CLEANMODELS_QT_TESTS "Build the unit tests" ON)
if(CLEANMODELS_QT_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(tst_animroot tests/tst_animroot.cpp mdlscene.cpp)
    target_include_directories(tst_animroot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_animroot PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME tst_animroot COMMAND tst_animroot)

    add_executable(tst_cacheprune tests/tst_cacheprune.cpp)
    target_include_directories(tst_cacheprune PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_cacheprune PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_cacheprune COMMAND tst_cacheprune)
endif()
