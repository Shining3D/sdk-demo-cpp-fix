cmake_minimum_required(VERSION 3.4)
cmake_policy(VERSION 3.4)
project(FixScan-Demo VERSION 0.1.0)
 
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set (CMAKE_PREFIX_PATH $ENV{QTDIR595_64})

find_package(Qt5 COMPONENTS Core Gui Widgets LinguistTools REQUIRED)

set(TARGET_NAME FixScan-Demo)

set(UIS
	mainwindow.ui
    progressdialog.ui
	mesh.ui
	save.ui
	startscan.ui
	simplify.ui
)

set(HEADERS 
	mainwindow.h
    progressdialog.h
    subscriber.h
    dataprocesser.h
	commonui.h
	mesh.h
	save.h
	startscan.h
	simplify.h
	reporterror.h
)

set(SOURCES 
	main.cpp
	mainwindow.cpp
    progressdialog.cpp
    subscriber.cpp
    dataprocesser.cpp
	commonui.cpp
	mesh.cpp
	save.cpp
	startscan.cpp
	simplify.cpp
	reporterror.cpp
)

include_directories(${ZeroMQ_INCLUDE_DIR})

find_package(ZeroMQ PATHS ${CMAKE_SOURCE_DIR}/ZeroMQ/CMake REQUIRED)


add_executable(${TARGET_NAME} ${HEADERS} ${SOURCES} ${UIS})

if(MSVC)
    set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "/MP")
endif()

target_link_libraries(${TARGET_NAME} Qt5::Core Qt5::Gui Qt5::Widgets libzmq-static)
