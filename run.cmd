@echo off
setlocal

set "GST_ROOT=%~dp0gst"

set "PATH=%GST_ROOT%\bin;%PATH%"

set "GST_PLUGIN_PATH=%GST_ROOT%\lib\gstreamer-1.0"
set "GST_PLUGIN_SYSTEM_PATH=%GST_ROOT%\lib\gstreamer-1.0"

"%~dp0webrtc-ui.exe"
