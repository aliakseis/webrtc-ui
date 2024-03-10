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


#find_package(PkgConfig)

#pkg_check_modules(GSTREAMER REQUIRED 
#  glib-2.0
#  json-glib-1.0
#  libsoup-2.4
#  gstreamer-1.0 
#  gstreamer-video-1.0
#  gstreamer-sdp-1.0
#  gstreamer-rtp-1.0
#  gstreamer-webrtc-1.0
#)

#message("GSTREAMER_INCLUDE_DIRS: ${GSTREAMER_INCLUDE_DIRS}")

#message("GSTREAMER_LIBRARY_DIRS: ${GSTREAMER_LIBRARY_DIRS}")


#link_directories(${GSTREAMER_LIBRARY_DIRS})


include(cmake/detect_gstreamer.cmake)

if (HAVE_GSTREAMER)
	message ("    GStreamer: (${GSTREAMER_VERSION})")
else()
	message( FATAL_ERROR "GStreamer not found." )
endif()


find_package(crossguid CONFIG REQUIRED)

find_package(curl CONFIG REQUIRED)



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
    http.cpp
    http.h
    sendrecv.cpp
    sendrecv.h
    signaling_connection.cpp
    signaling_connection.h  
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
    http.cpp
    http.h
    sendrecv.cpp
    sendrecv.h
    signaling_connection.cpp
    signaling_connection.h  
    ${QRCS}
  )
endif()

target_include_directories(webrtc-ui PUBLIC ${GSTREAMER_INCLUDE_DIRS})

target_link_libraries(webrtc-ui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets ${GSTREAMER_LIBRARIES} crossguid CURL::libcurl)



if(MSVC)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/bin")

	set(BINARY_INSTALL_DIR .)

	install(TARGETS ${PROJECT_NAME} DESTINATION ${BINARY_INSTALL_DIR})


	set(BINARY_FILE "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe")

	message( STATUS "CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}" )

	get_filename_component(CMAKE_TOOLCHAIN_DIRECTORY ${CMAKE_TOOLCHAIN_FILE} DIRECTORY)

	#install(CODE "execute_process(COMMAND \"powershell\" \"-noprofile\" \"-executionpolicy\" \"Bypass\" \"-file\" \"${CMAKE_TOOLCHAIN_DIRECTORY}/msbuild/applocal.ps1\" \"-targetBinary\" \"${BINARY_FILE}\" \"-installedDir\" \"$ENV{GSTREAMER_ROOT_X86_64}/bin\" \"-OutVariable\" \"out\")")


	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	    set( CMAKE_INSTALL_PLATFORM "x64-windows" )
	else ()
	    set( CMAKE_INSTALL_PLATFORM "x86-windows" )
	endif ()

	install(CODE "execute_process(COMMAND \"powershell\" \"-noprofile\" \"-executionpolicy\" \"Bypass\" \"-file\" \"${CMAKE_TOOLCHAIN_DIRECTORY}/msbuild/applocal.ps1\" \"-targetBinary\" \"${BINARY_FILE}\" \"-installedDir\" \"${CMAKE_TOOLCHAIN_DIRECTORY}/../../installed/${CMAKE_INSTALL_PLATFORM}/bin\" \"-OutVariable\" \"out\")")

	# Install CRT
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
	#if(DEVELOPER_FEATURES)
	#	set(CMAKE_INSTALL_DEBUG_LIBRARIES ON)
	#endif(DEVELOPER_FEATURES)
	set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
	set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
	include (InstallRequiredSystemLibraries)

# Retrieve the absolute path to qmake and then use that path to find
# the windeployqt binary
	get_target_property(_qmake_executable Qt5::qmake IMPORTED_LOCATION)
	get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
	find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

	install(CODE "execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"  \"${BINARY_FILE}\" WORKING_DIRECTORY \"${_qt_bin_dir}\")")


	#if (QT_QMAKE_EXECUTABLE)
	#	get_filename_component(QT_BIN_DIRECTORY ${QT_QMAKE_EXECUTABLE} DIRECTORY)
	#	install(CODE "execute_process(COMMAND \"${QT_BIN_DIRECTORY}/windeployqt\"  \"${BINARY_FILE}\" WORKING_DIRECTORY \"${QT_BIN_DIRECTORY}\")")
	#else()
	#	install(CODE "execute_process(COMMAND \"windeployqt\"  \"${BINARY_FILE}\" )")
	#endif()


	#if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	#    install(DIRECTORY "$ENV{GSTREAMER_ROOT_X86_64}/bin/" DESTINATION .)
	#else ()
	#    install(DIRECTORY "$ENV{GSTREAMER_ROOT_X86}/bin/" DESTINATION .)
	#endif ()

	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	    install(DIRECTORY "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}/" DESTINATION gst PATTERN "*.pdb" EXCLUDE)
	else ()
	    install(DIRECTORY "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86}/" DESTINATION gst PATTERN "*.pdb" EXCLUDE)
	endif ()

	install(FILES run.cmd DESTINATION .)

endif()
