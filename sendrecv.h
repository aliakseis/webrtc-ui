#pragma once

#include <string>
#include <functional>

#include <glib.h> // for gchar, gboolean

// Use a wide string for paths on Windows, UTF-8 std::string elsewhere.
#ifdef _WIN32
using PathString = std::wstring;
#else
using PathString = std::string;
#endif

struct Settings
{
    PathString save_path;              // directory for recordings
    bool do_save = false;              // enable saving incoming streams
    bool use_turn = false;             // enable TURN usage
    std::string turn_server;           // TURN server (host[:port] or full address)
    std::string video_launch_line;     // pipeline fragment for video source
    std::string audio_launch_line;     // pipeline fragment for audio source
    int slice_duration_secs = 0;       // >0 => enable splitmuxsink slicing
};

// Forward declare ISendRecv to avoid header dependency
struct ISendRecv;

// External API used by the rest of the program
void cleanup_and_quit_loop(const gchar* msg, bool is_error);
bool set_connected();
void on_server_message(const gchar* text);
gboolean start_pipeline(gboolean create_offer);

// Start sendrecv: pass platform settings by value (copied into the worker).
bool start_sendrecv(unsigned long long winid, ISendRecv* isendrecv, Settings settings);

