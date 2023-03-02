cmake_minimum_required(VERSION 3.5)

project(webrtc-ui LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()



find_package(PkgConfig)

pkg_check_modules(GSTREAMER REQUIRED 
  glib-2.0
  json-glib-1.0
  libsoup-2.4
  gstreamer-1.0 
  gstreamer-video-1.0
  gstreamer-sdp-1.0
  gstreamer-rtp-1.0
  gstreamer-webrtc-1.0
)

message("GSTREAMER_INCLUDE_DIRS: ${GSTREAMER_INCLUDE_DIRS}")

message("GSTREAMER_LIBRARY_DIRS: ${GSTREAMER_LIBRARY_DIRS}")


link_directories(${GSTREAMER_LIBRARY_DIRS})




find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

# Generate rules for building source files from the resources
qt5_add_resources(QRCS resources.qrc)

if(ANDROID)
  add_library(webrtc-ui SHARED
    main.cpp
    globals.h
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    maintoolbar.cpp
    maintoolbar.h
    maintoolbar.ui
    preferences.cpp
    preferences.h
    preferences.ui
    cameraman.cpp
    cameraman.h
    ${QRCS}
  )
else()
  add_executable(webrtc-ui
    main.cpp
    globals.h
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    maintoolbar.cpp
    maintoolbar.h
    maintoolbar.ui
    preferences.cpp
    preferences.h
    preferences.ui
    cameraman.cpp
    cameraman.h
    ${QRCS}
  )
endif()

target_include_directories(webrtc-ui PUBLIC ${GSTREAMER_INCLUDE_DIRS})

target_link_libraries(webrtc-ui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${GSTREAMER_LIBRARIES})
