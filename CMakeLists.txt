cmake_minimum_required(VERSION 3.17)
project(cleanmodels-qt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(QT_COMPONENTS Core Widgets Gui)

if(DEFINED ENV{BUILD_STATIC})
    set(CMAKE_PREFIX_PATH "/usr/local/Qt-5.15.2/lib/cmake/Qt5")
    set(QT_ROOT "/usr/local/Qt-5.15.2/lib/cmake/")
endif()

find_package(Qt5 COMPONENTS ${QT_COMPONENTS} REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(DEFINED ENV{BUILD_STATIC})
    include("./CMakeModules/Qt.cmake")
endif()

add_executable(${PROJECT_NAME} main.cpp mainwindow.cpp mainwindow_clean.cpp fsmodel.cpp icons.qrc prolog_files.qrc mainwindow.ui)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Widgets Qt5::Gui)

if(DEFINED ENV{BUILD_STATIC})
    qt5_import_plugins(${PROJECT_NAME}
            EXCLUDE_BY_TYPE imageformats
            )
endif()