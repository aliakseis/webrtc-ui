#include "sendrecv.h"

#include "isendrecv.h"

/*
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 * gcc webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o webrtc-sendrecv
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/rtp/rtp.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#include <gst/video/videooverlay.h>

/* For signalling */
#include "signaling_connection.h"

#include <json-glib/json-glib.h>


#include "globals.h"

#include "makeguard.h"

#include <QSettings>
#include <QDateTime>
#include <QFile>
#include <QSlider>

#include <cstring>

#include <string>
#include <string_view>

#include <atomic>
#include <memory>
#include <mutex>

#define GST_CAT_DEFAULT webrtc_sendrecv_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);



static gchar*
get_string_from_json_object(JsonObject* object)
{
    /* Make it the root node */
    auto root = json_node_init_object(json_node_alloc(), object);
    auto generator = json_generator_new();
    json_generator_set_root(generator, root);
    auto text = json_generator_to_data(generator, nullptr);

    /* Release everything */
    g_object_unref(generator);
    json_node_free(root);
    return text;
}



#ifdef _WIN64
#define DEFAULT_VIDEOSINK "d3d11videosink"
#else
#define DEFAULT_VIDEOSINK "d3dvideosink"
#endif

/* slightly convoluted way to find a working video sink that's not a bin,
 * one could use autovideosink from gst-plugins-good instead
 */
static GstElement*
find_video_sink()
{
    GstElement* sink;

    if ((sink = gst_element_factory_make("xvimagesink", nullptr))) {
        auto sret = gst_element_set_state(sink, GST_STATE_READY);
        if (sret == GST_STATE_CHANGE_SUCCESS)
            return sink;

        gst_element_set_state(sink, GST_STATE_NULL);
        gst_object_unref(sink);
    }

    if ((sink = gst_element_factory_make("ximagesink", nullptr))) {
        auto sret = gst_element_set_state(sink, GST_STATE_READY);
        if (sret == GST_STATE_CHANGE_SUCCESS)
            return sink;

        gst_element_set_state(sink, GST_STATE_NULL);
        gst_object_unref(sink);
    }

    if (strcmp(DEFAULT_VIDEOSINK, "xvimagesink") == 0 ||
        strcmp(DEFAULT_VIDEOSINK, "ximagesink") == 0)
        return nullptr;

    if ((sink = gst_element_factory_make(DEFAULT_VIDEOSINK, nullptr))) {
        if (GST_IS_BIN(sink)) {
            gst_object_unref(sink);
            return nullptr;
        }

        auto sret = gst_element_set_state(sink, GST_STATE_READY);
        if (sret == GST_STATE_CHANGE_SUCCESS)
            return sink;

        gst_element_set_state(sink, GST_STATE_NULL);
        gst_object_unref(sink);
    }

    return nullptr;
}

static GstPadProbeReturn
static_rtp_packet_loss_probe(GstPad* opad, GstPadProbeInfo* p_info, gpointer /*p_data*/)
{
    if (G_UNLIKELY((p_info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) == 0))
        return GST_PAD_PROBE_OK;

    GstEvent* event = gst_pad_probe_info_get_event(p_info);
    switch (GST_EVENT_TYPE(event))
    {
    case GST_EVENT_GAP:
    {
        static GstClockTime prev_ts{};
        GstClockTime ts, dur;
        gst_event_parse_gap(event, &ts, &dur);
        if (prev_ts != ts)
        {
            prev_ts = ts;
            GstClockTime end = ts;
            if (ts != GST_CLOCK_TIME_NONE && dur != GST_CLOCK_TIME_NONE)
                end += dur;
            g_print("%s:%s Gap TS: %" GST_TIME_FORMAT " dur %" GST_TIME_FORMAT
                " (to %" GST_TIME_FORMAT ")\n", GST_DEBUG_PAD_NAME(opad),
                GST_TIME_ARGS(ts), GST_TIME_ARGS(dur), GST_TIME_ARGS(end));
        }
        break;
    }

    default:
        break;
    }

    return GST_PAD_PROBE_OK;
}


static void
disconnect(gpointer data,
    GObject* where_the_object_was)
{
    auto c = static_cast<QMetaObject::Connection*>(data);
    auto ok = QObject::disconnect(*c);
    g_assert_true(ok);
    delete c;
}

class GObjHandle
{
public:
    GObjHandle(gpointer p)
    {
        g_weak_ref_init(&m_ref, p);
    }
    ~GObjHandle()
    {
        if (m_valid)
            g_weak_ref_clear(&m_ref);
    }
    GObjHandle(GObjHandle&& other)  noexcept : m_ref(other.m_ref)
    {
        other.m_valid = false;
    }
    GObjHandle(const GObjHandle&) = delete;
    GObjHandle operator =(const GObjHandle&) = delete;

    auto get() const
    {
        auto ptr = g_weak_ref_get(&m_ref);
        return MakeGuard(ptr, g_object_unref);
    }

private:
    mutable GWeakRef m_ref;
    bool m_valid = true;
};


static auto prepare_next_file_name()
{
    QDateTime now = QDateTime::currentDateTime();
    const auto name = now.toString("yyMMddhhmmss");
    auto path = QSettings().value(SETTING_SAVE_PATH).toString() + '/' + name + ".webm";
    int i = 0;
    while (QFile::exists(path))
    {
        ++i;
        path = QSettings().value(SETTING_SAVE_PATH).toString() + '/' + name + '(' + QString::number(i) + ").webm";
    }
    return QFile::encodeName(path);
}

static gchar* splitmuxsink_on_format_location_full(GstElement* splitmux,
    guint fragment_id,
    GstSample* first_sample,
    gpointer user_data)
{
    auto nextfilename = prepare_next_file_name();
    g_print("New file name generated for recording as %s \n", nextfilename.constData());
    return g_strdup_printf("%s", nextfilename.constData());
}

static gboolean
check_plugins()
{
    const gchar* needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
      "rtpmanager", "videotestsrc", "audiotestsrc", nullptr
    };

    auto registry = gst_registry_get();
    gboolean ret = TRUE;
    for (guint i = 0; i < g_strv_length((gchar**)needed); i++) {
        auto plugin = gst_registry_find_plugin(registry, needed[i]);
        if (!plugin) {
            gst_print("Required gstreamer plugin '%s' not found\n", needed[i]);
            ret = FALSE;
            continue;
        }
        gst_object_unref(plugin);
    }
    return ret;
}


////////////////////////////////////////////////////////////////////


enum AppState
{
    APP_STATE_UNKNOWN = 0,
    APP_STATE_ERROR = 1,          /* generic error */
    SERVER_CONNECTING = 1000,
    SERVER_CONNECTION_ERROR,
    SERVER_CONNECTED,             /* Ready to register */
    SERVER_REGISTERING = 2000,
    SERVER_REGISTRATION_ERROR,
    SERVER_REGISTERED,            /* Ready to call a peer */
    SERVER_CLOSED,                /* server connection closed by us or the server */
    PEER_CONNECTING = 3000,
    PEER_CONNECTION_ERROR,
    PEER_CONNECTED,
    PEER_CALL_NEGOTIATING = 4000,
    PEER_CALL_STARTED,
    PEER_CALL_STOPPING,
    PEER_CALL_STOPPED,
    PEER_CALL_ERROR,
    HANG_UP
};

const static gboolean remote_is_offerer = FALSE;

class SendRecv {

GMainLoop *loop = nullptr;
GstElement *pipe1 = nullptr;
GstElement *webrtc1 = nullptr;
GObject *send_channel = nullptr;
GObject *receive_channel = nullptr;

AppState app_state = APP_STATE_UNKNOWN;

guint webrtcbin_get_stats_id = 0;

std::vector<std::pair<int, std::string>> ice_candidates;

guintptr xwinid{};

QSlider* g_volume_notifier = nullptr;

ISendRecv* p_sendrecv = nullptr;



std::unique_ptr<ISignalingConnection> signaling_connection;

GThread* gthread = nullptr;

GstClockTime last_video_pts{};

std::mutex mtx;


////////////////////////////////////////////////////////////////////

public:

bool set_connected()
{
    if (app_state < PEER_CONNECTED) {
        app_state = PEER_CONNECTED;
        return true;
    }
    return false;
}

gboolean cleanup_and_quit_loop (const gchar * msg, enum AppState state)
{
  if (msg)
    gst_printerr ("%s\n", msg);
  if (state > 0)
    app_state = state;

  if (webrtcbin_get_stats_id)
    g_source_remove(webrtcbin_get_stats_id);
  webrtcbin_get_stats_id = 0;

  if (signaling_connection)
    signaling_connection->close();

  if (loop) {
    g_main_loop_quit (loop);
    g_clear_pointer (&loop, g_main_loop_unref);
  }

  if (p_sendrecv)
  {
      p_sendrecv->onQuit();
      p_sendrecv = nullptr;
  }

  /* To allow usage as a GSourceFunc */
  return G_SOURCE_REMOVE;
}

void cleanup_and_quit_loop(const gchar* msg, bool is_error)
{
    cleanup_and_quit_loop(msg, is_error ? PEER_CALL_ERROR : HANG_UP);
}

void handle_media_stream(GstPad* pad, GstElement* pipe, const char* convert_name,
    GstElement* sink)
{
    auto q = gst_element_factory_make("queue", nullptr);
    g_assert_nonnull(q);
    auto conv = gst_element_factory_make(convert_name, nullptr);
    g_assert_nonnull(conv);
    g_assert_nonnull(sink);

    if (g_strcmp0(convert_name, "audioconvert") == 0) {
        /* Might also need to resample, so add it just in case.
         * Will be a no-op if it's not required. */
        auto resample = gst_element_factory_make("audioresample", nullptr);
        g_assert_nonnull(resample);

        auto volume = gst_element_factory_make("volume", nullptr);
        auto c = new QMetaObject::Connection(
            QObject::connect(g_volume_notifier, &QSlider::valueChanged, [ptr = GObjHandle(volume)](int v) {
                if (auto obj = ptr.get())
                    g_object_set(obj.get(), "volume", v / 100., NULL);
                })
        );
        g_object_weak_ref(G_OBJECT(volume), disconnect, c);

        gst_bin_add_many(GST_BIN(pipe), q, conv, resample, volume, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(resample);
        gst_element_sync_state_with_parent(volume);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, resample, volume, sink, NULL);
    }
    else {
        // adding a probe for handling loss messages from rtpbin
        gst_pad_add_probe(pad,
            GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
            static_rtp_packet_loss_probe,
            nullptr,
            nullptr);

        gst_bin_add_many(GST_BIN(pipe), q, conv, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, sink, NULL);
    }

    auto qpad = gst_element_get_static_pad(q, "sink");

    auto ret = gst_pad_link(pad, qpad);
    g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
}


static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  if (!gst_pad_has_current_caps (pad)) {
    gst_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
        GST_PAD_NAME (pad));
    return;
  }

  auto caps = gst_pad_get_current_caps (pad);
  auto name = gst_structure_get_name (gst_caps_get_structure (caps, 0));

  auto str = gst_caps_to_string(caps);
  g_print("on_incoming_decodebin_stream pad caps: %s\n", str);
  g_free(str);

  if (g_str_has_prefix (name, "video")) {
    auto sink = find_video_sink();
    self->handle_media_stream (pad, self->pipe1, "videoconvert", sink);
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink), self->xwinid);
  } else if (g_str_has_prefix (name, "audio")) {
      self->handle_media_stream (pad, self->pipe1, "audioconvert", gst_element_factory_make("autoaudiosink", nullptr));
  } else {
    gst_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }
}


GstElement* get_file_sink(GstBin* pipe)
{
    std::unique_lock<std::mutex> lock(mtx);

    const char file_sink_name[] = "file_sink";
    if (auto result = gst_bin_get_by_name(pipe, file_sink_name))
        return result;

    const char muxerName[] = "webmmux";

    const int sliceDurationSecs = getSliceDurationSecs();
    if (sliceDurationSecs > 0)
    {
        auto splitmuxsink = gst_element_factory_make("splitmuxsink", file_sink_name);
        auto s = gst_structure_new("properties",
            "streamable", G_TYPE_BOOLEAN, TRUE,
            nullptr);
        g_object_set(G_OBJECT(splitmuxsink),
            "async-finalize", TRUE,
            "max-size-time", GST_SECOND * sliceDurationSecs,
            "muxer-factory", muxerName,
            "muxer-properties", s,
            NULL);
        g_signal_connect(splitmuxsink, "format-location-full",
            G_CALLBACK(splitmuxsink_on_format_location_full), NULL);

        auto ok = gst_bin_add(GST_BIN(pipe), splitmuxsink);
        g_assert_true(ok);

        ok = gst_element_sync_state_with_parent(splitmuxsink);
        g_assert_true(ok);

        return splitmuxsink;
    }
    
    auto muxer = gst_element_factory_make(muxerName, file_sink_name);
    auto ok = gst_bin_add(GST_BIN(pipe), muxer);
    g_assert_true(ok);

    ok = gst_element_sync_state_with_parent(muxer);
    g_assert_true(ok);

    auto filesink = gst_element_factory_make("filesink", nullptr);
    ok = gst_bin_add(GST_BIN(pipe), filesink);
    g_assert_true(ok);

    auto nextfilename = prepare_next_file_name();
    g_object_set(G_OBJECT(filesink),
        "location", nextfilename.constData(),
        NULL);

    ok = gst_element_sync_state_with_parent(filesink);
    g_assert_true(ok);

    ok = gst_element_link_many(
        muxer,
        filesink,
        NULL);
    g_assert_true(ok);

    return muxer;
}


// https://stackoverflow.com/questions/29107370/gstreamer-timestamps-pts-are-not-monotonically-increasing-for-captured-frames
static GstPadProbeReturn
gst_pad_probe_callback(GstPad * pad,
    GstPadProbeInfo * info,
    gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

    auto buffer = gst_pad_probe_info_get_buffer(info);

    auto pts = buffer->pts;
    if (pts <= self->last_video_pts)
    {
        g_print("Out-of-order pts: %lld; the last pts: %lld.\n", pts, self->last_video_pts);
        buffer->pts = self->last_video_pts;
        return GST_PAD_PROBE_OK;
    }
    self->last_video_pts = pts;

    return GST_PAD_PROBE_OK;
}


static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, gpointer user_data)
{
  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  auto self = static_cast<SendRecv*>(user_data);

  auto decodebin = gst_element_factory_make ("decodebin", nullptr);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), self);
  gst_bin_add (GST_BIN (self->pipe1), decodebin);
  gst_element_sync_state_with_parent (decodebin);

  auto caps = gst_pad_get_current_caps(pad);
  auto str = gst_caps_to_string(caps);
  g_print("on_incoming_stream pad caps: %s\n", str);
  g_free(str);

  int payload = 0;
  if (QSettings().value(SETTING_DO_SAVE).toBool())
  {
      GstStructure *s = gst_caps_get_structure(caps, 0);
      auto ok = gst_structure_get_int(s, "payload", &payload);
      g_assert_true(ok);
  }
  if (payload == 96 || payload == 97)
  {
      auto tee = gst_element_factory_make("tee", nullptr);
      gst_bin_add(GST_BIN(self->pipe1), tee);
      gst_element_sync_state_with_parent(tee);

      {
          auto sinkpad = gst_element_get_static_pad(tee, "sink");
          auto ret = gst_pad_link(pad, sinkpad);
          g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
          gst_object_unref(sinkpad);
      }
      {
          auto srcpad = gst_element_request_pad_simple(tee, "src_%u");
          auto sinkpad = gst_element_get_static_pad(decodebin, "sink");
          auto ret = gst_pad_link(srcpad, sinkpad);
          g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
          gst_object_unref(srcpad);
          gst_object_unref(sinkpad);
      }

      auto rtpvp8depay = gst_element_factory_make(
          (payload == 96) ? "rtpvp8depay" : "rtpopusdepay", nullptr);

      auto ok = gst_bin_add(GST_BIN(self->pipe1), rtpvp8depay);
      g_assert_true(ok);

      ok = gst_element_sync_state_with_parent(rtpvp8depay);
      g_assert_true(ok);

      auto queue = gst_element_factory_make("queue", nullptr);

      ok = gst_bin_add(GST_BIN(self->pipe1), queue);
      g_assert_true(ok);

      ok = gst_element_sync_state_with_parent(queue);
      g_assert_true(ok);

      auto sink = self->get_file_sink(GST_BIN(self->pipe1));

      if (payload == 96)
      {
        self->last_video_pts = {};

        auto srcpad = gst_element_get_static_pad(rtpvp8depay, "src");
        gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, gst_pad_probe_callback, self, nullptr);

        ok = gst_element_link_many(tee,
            //rtpjitterbuffer,
            rtpvp8depay,
            queue,
            //sink,
            NULL);
        g_assert_true(ok);
      }
      else
      {
          auto opusdec = gst_element_factory_make("opusdec", nullptr);
          ok = gst_bin_add(GST_BIN(self->pipe1), opusdec);
          g_assert_true(ok);
          ok = gst_element_sync_state_with_parent(opusdec);
          g_assert_true(ok);

          //audiorate
          auto audiorate = gst_element_factory_make("audiorate", nullptr);
          ok = gst_bin_add(GST_BIN(self->pipe1), audiorate);
          g_assert_true(ok);
          ok = gst_element_sync_state_with_parent(audiorate);
          g_assert_true(ok);

          //opusenc
          auto opusenc = gst_element_factory_make("opusenc", nullptr);
          ok = gst_bin_add(GST_BIN(self->pipe1), opusenc);
          g_assert_true(ok);
          ok = gst_element_sync_state_with_parent(opusenc);
          g_assert_true(ok);

          ok = gst_element_link_many(tee,
              rtpvp8depay,
              opusdec,
              audiorate,
              opusenc,
              queue,
              NULL);
          g_assert_true(ok);
      }


      auto srcpad = gst_element_get_static_pad(queue, "src");
      auto sinkpad = gst_element_request_pad_simple(sink,
            (payload == 97) ? "audio_%u" : ((getSliceDurationSecs() > 0) ? "video" : "video_%u"));
      auto ret = gst_pad_link(srcpad, sinkpad);
      g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
      gst_object_unref(srcpad);
      gst_object_unref(sinkpad);
  }
  else
  {
      auto sinkpad = gst_element_get_static_pad(decodebin, "sink");
      gst_pad_link(pad, sinkpad);
      gst_object_unref(sinkpad);
  }
}


static void
send_ice_candidate_message(GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    self->ice_candidates.emplace_back( mlineindex, candidate );
}


void send_candidates()
{
    auto ar = json_array_new();

    for (const auto& candidate : ice_candidates)
    {
        auto ice = json_object_new();
        json_object_set_string_member(ice, "candidate", candidate.second.c_str());
        json_object_set_int_member(ice, "sdpMLineIndex", candidate.first);
        json_array_add_object_element(ar, ice);
    }

    ice_candidates.clear();

    auto msg = json_object_new();
    json_object_set_array_member(msg, "ice", ar);
    auto text = get_string_from_json_object(msg);
    json_object_unref(msg);

    signaling_connection->send_text(text);
    g_free(text);
}


void send_sdp_to_peer (GstWebRTCSessionDescription * desc)
{
  if (app_state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send SDP to peer, not in call",
        APP_STATE_ERROR);
    return;
  }

  auto text = gst_sdp_message_as_text (desc->sdp);
  auto sdp = json_object_new ();

  if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
    gst_print ("Sending offer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "offer");
  } else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    gst_print ("Sending answer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "answer");
  } else {
    g_assert_not_reached ();
  }

  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  auto msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  signaling_connection->send_text(text);
  g_free (text);
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  GstWebRTCSessionDescription *offer = nullptr;
  const GstStructure *reply;

  g_assert_cmphex (self->app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (self->webrtc1, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  self->send_sdp_to_peer (offer);
  gst_webrtc_session_description_free (offer);
}

void on_negotiation_needed (GstElement * element, bool create_offer)
{
  app_state = PEER_CALL_NEGOTIATING;

  if (remote_is_offerer) {
      signaling_connection->send_text("OFFER_REQUEST");
  } else if (create_offer) {
    GstPromise *promise =
        gst_promise_new_with_change_func (on_offer_created, this, nullptr);
    g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
  }
}

static void
on_negotiation_needed_true(GstElement* element, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    self->on_negotiation_needed(element, true);
}

static void
on_negotiation_needed_false(GstElement* element, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    self->on_negotiation_needed(element, false);
}

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

static void
data_channel_on_error (GObject * dc, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    self->cleanup_and_quit_loop ("Data channel error", APP_STATE_UNKNOWN);
}


static void
data_channel_on_close (GObject * dc, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    self->cleanup_and_quit_loop ("Data channel closed", APP_STATE_UNKNOWN);
}

static void
data_channel_on_message_string (GObject * dc, gchar * str, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

    if (self->p_sendrecv)
        self->p_sendrecv->handleRecv((uintptr_t)(void*) dc, str);
}

void connect_data_channel_signals (GObject * data_channel)
{
  g_signal_connect (data_channel, "on-error",
      G_CALLBACK (data_channel_on_error), this);
  g_signal_connect (data_channel, "on-close",
      G_CALLBACK (data_channel_on_close), this);
  g_signal_connect (data_channel, "on-message-string",
      G_CALLBACK (data_channel_on_message_string), this);
}

static void
on_data_channel (GstElement * webrtc, GObject * data_channel,
    gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  self->connect_data_channel_signals (data_channel);
  self->receive_channel = data_channel;
  if (self->p_sendrecv)
  {
      auto lam = [self](const QString& s) {
          auto line = s.toStdString();
          if (!line.empty() && self->receive_channel)
              g_signal_emit_by_name(self->receive_channel, "send-string", line.c_str());
          };

      self->p_sendrecv->setSendLambda(lam);
  }

}

static void
on_ice_gathering_state_notify (GstElement * webrtcbin, GParamSpec * pspec,
    gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  GstWebRTCICEGatheringState ice_gather_state;
  const gchar *new_state = "unknown";

  g_object_get (webrtcbin, "ice-gathering-state", &ice_gather_state, NULL);
  switch (ice_gather_state) {
    case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
      new_state = "new";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
      new_state = "gathering";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
      new_state = "complete";
      self->send_candidates();
      break;
  }
  gst_print ("ICE gathering state changed to %s\n", new_state);
}

static void
on_ice_connection_state_notify(GstElement * webrtcbin, GParamSpec * pspec,
    gpointer user_data)
{
    GstWebRTCICEConnectionState ice_connection_state;
    const gchar *new_state = "unknown";

    g_object_get(webrtcbin, "ice-connection-state", &ice_connection_state, NULL);
    switch (ice_connection_state) {
    case GST_WEBRTC_ICE_CONNECTION_STATE_NEW:
        new_state = "new";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING:
        new_state = "checking";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
        new_state = "connected";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
        new_state = "completed";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
        new_state = "failed";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
        new_state = "disconnected";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED:
        new_state = "closed";
        break;
    }
    gst_print("ICE connection state changed to %s\n", new_state);
}


static gboolean
on_webrtcbin_stat (GQuark field_id, const GValue * value, gpointer unused)
{
  if (GST_VALUE_HOLDS_STRUCTURE (value)) {
    GST_DEBUG ("stat: \'%s\': %" GST_PTR_FORMAT, g_quark_to_string (field_id),
        gst_value_get_structure (value));
  } else {
    GST_FIXME ("unknown field \'%s\' value type: \'%s\'",
        g_quark_to_string (field_id), g_type_name (G_VALUE_TYPE (value)));
  }

  return TRUE;
}

static void
on_webrtcbin_get_stats (GstPromise * promise, void* user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  const GstStructure *stats;

  g_return_if_fail (gst_promise_wait (promise) == GST_PROMISE_RESULT_REPLIED);

  stats = gst_promise_get_reply (promise);
  gst_structure_foreach (stats, on_webrtcbin_stat, nullptr);

  self->webrtcbin_get_stats_id = g_timeout_add (100, (GSourceFunc) webrtcbin_get_stats, self);
}

static gboolean
webrtcbin_get_stats (void* user_data)
{
    auto self = static_cast<SendRecv*>(user_data);
    
    GstPromise *promise =
      gst_promise_new_with_change_func (
      (GstPromiseChangeFunc) on_webrtcbin_get_stats, self, nullptr);

  GST_TRACE ("emitting get-stats on %" GST_PTR_FORMAT, self->webrtc1);
  g_signal_emit_by_name (self->webrtc1, "get-stats", NULL, promise);
  gst_promise_unref (promise);

  return G_SOURCE_REMOVE;
}


static void
on_new_transceiver(GstElement * webrtc, GstWebRTCRTPTransceiver * trans)
{
    /* If we expected more than one transceiver, we would take a look at
     * trans->mline, and compare it with webrtcbin's local description */
    g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "do-nack", TRUE, NULL);
}

static gboolean bus_call(GstBus * /*bus*/, GstMessage *msg, void *user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *err;
        gchar *debug;
        gst_message_parse_error(msg, &err, &debug);
        g_print("GOT ERROR %s\n", err->message);
        g_error_free(err);
        g_free(debug);

        if (auto srcName = GST_MESSAGE_SRC_NAME(msg))
        {
            g_print("ERROR SOURCE %s\n", srcName);
            // could get and handle source: GST_MESSAGE_SRC(msg);
        }

        break;
    }

    case GST_MESSAGE_LATENCY:
    {
        // when pipeline latency is changed, this msg is posted on the bus. we then have
        // to explicitly tell the pipeline to recalculate its latency
        if (self->pipe1 && !gst_bin_recalculate_latency(GST_BIN(self->pipe1)))
            g_print("Could not reconfigure latency.\n");
        else
            g_print("Reconfigured latency.\n");
        break;
    }
    default:
        break;
    }

    return TRUE;
}

#define RTP_TWCC_URI "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

gboolean
start_pipeline (gboolean create_offer)
{
  GstStateChangeReturn ret;
  GError *error = nullptr;

  std::string turnServer;
  if (QSettings().value(SETTING_USE_TURN).toBool())
  {
      turnServer = QSettings().value(SETTING_TURN).toString().trimmed().toStdString();
      if (!turnServer.empty())
      {
          turnServer = " turn-server=turn://" + turnServer + ' ';
      }
  }

  const auto pipeline_description = "webrtcbin bundle-policy=max-bundle name=sendrecv "
      STUN_SERVER + turnServer
      + QSettings().value(SETTING_VIDEO_LAUNCH_LINE, VIDEO_LAUNCH_LINE_DEFAULT).toString().toStdString() +
      " ! videoconvert ! queue ! "
      // https://developer.ridgerun.com/wiki/index.php/GstKinesisWebRTC/Getting_Started/C_Example_Application
      "vp8enc error-resilient=partitions keyframe-max-dist=10 deadline=1 ! "
      // picture-id-mode=15-bit seems to make TWCC stats behave better
      "rtpvp8pay name=videopay picture-id-mode=15-bit ! "
      "queue ! " RTP_CAPS_VP8 "96 ! sendrecv. "
      + QSettings().value(SETTING_AUDIO_LAUNCH_LINE, AUDIO_LAUNCH_LINE_DEFAULT).toString().toStdString() +
      " ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay name=audiopay ! "
      "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. ";

  pipe1 = gst_parse_launch (pipeline_description.c_str(), &error);

  if (error) {
    gst_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  // add bus call
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe1));
  gst_bus_add_watch(bus, bus_call, this);
  gst_object_unref(bus);

  webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "sendrecv");
  g_assert_nonnull (webrtc1);

  if (remote_is_offerer) {
    /* XXX: this will fail when the remote offers twcc as the extension id
     * cannot currently be negotiated when receiving an offer.
     */
    GST_FIXME ("Need to implement header extension negotiation when "
        "reciving a remote offers");
  } else {

    auto lam = [this] (const gchar* name) {
        auto videopay = gst_bin_get_by_name(GST_BIN(pipe1), name);
        g_assert_nonnull(videopay);
        auto video_twcc = gst_rtp_header_extension_create_from_uri(RTP_TWCC_URI);
        g_assert_nonnull(video_twcc);
        gst_rtp_header_extension_set_id(video_twcc, 1);
        g_signal_emit_by_name(videopay, "add-extension", video_twcc);
        g_clear_object(&video_twcc);
        g_clear_object(&videopay);
    };

    lam("videopay");
    lam("audiopay");
  }

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc1, "on-negotiation-needed",
      create_offer? G_CALLBACK (on_negotiation_needed_true) : G_CALLBACK(on_negotiation_needed_false),
      this);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc1, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), this);
  g_signal_connect (webrtc1, "notify::ice-gathering-state",
      G_CALLBACK (on_ice_gathering_state_notify), this);
  g_signal_connect(webrtc1, "on-new-transceiver",
      G_CALLBACK(on_new_transceiver), NULL);
  g_signal_connect(webrtc1, "notify::ice-connection-state",
      G_CALLBACK(on_ice_connection_state_notify), NULL);

  gst_element_set_state (pipe1, GST_STATE_READY);

  g_signal_emit_by_name (webrtc1, "create-data-channel", "channel", NULL,
      &send_channel);
  if (send_channel) {
    gst_print ("Created data channel\n");
    connect_data_channel_signals (send_channel);

    if (p_sendrecv)
    {
        auto lam = [this](const QString& s) {
            auto line = s.toStdString();
            if (!line.empty() && send_channel)
                g_signal_emit_by_name(send_channel, "send-string", line.c_str());
        };

        p_sendrecv->setSendLambda(lam);
    }

  } else {
    gst_print ("Could not create data channel, is usrsctp available?\n");
  }

  g_signal_connect (webrtc1, "on-data-channel", G_CALLBACK (on_data_channel), this);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream), this);
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1);

  webrtcbin_get_stats_id = g_timeout_add (100, (GSourceFunc) webrtcbin_get_stats, this);

  gst_print ("Starting pipeline\n");
  ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  return TRUE;

err:
  if (pipe1)
    g_clear_object (&pipe1);
  if (webrtc1)
    webrtc1 = nullptr;
  return FALSE;
}


/* Answer created by our pipeline, to be sent to the peer */
static void
on_answer_created (GstPromise * promise, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  GstWebRTCSessionDescription *answer = nullptr;

  g_assert_cmphex (self->app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  auto reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (self->webrtc1, "set-local-description", answer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send answer to peer */
  self->send_sdp_to_peer (answer);
  gst_webrtc_session_description_free (answer);
}

static void
on_offer_set (GstPromise * promise, gpointer user_data)
{
    auto self = static_cast<SendRecv*>(user_data);

  gst_promise_unref (promise);
  promise = gst_promise_new_with_change_func (on_answer_created, self, nullptr);
  g_signal_emit_by_name (self->webrtc1, "create-answer", NULL, promise);
}

void on_offer_received (GstSDPMessage * sdp)
{
  GstWebRTCSessionDescription *offer = nullptr;
  GstPromise *promise;

  offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  g_assert_nonnull (offer);

  /* Set remote description on our pipeline */
  {
    promise = gst_promise_new_with_change_func (on_offer_set, this, nullptr);
    g_signal_emit_by_name (webrtc1, "set-remote-description", offer, promise);
  }
  gst_webrtc_session_description_free (offer);
}

/* One mega message handler for our asynchronous calling mechanism */
void on_server_message(const gchar *text) {
  if (g_strcmp0 (text, "OFFER_REQUEST") == 0) {
    if (app_state != SERVER_REGISTERED) {
      gst_printerr ("Received OFFER_REQUEST at a strange time, ignoring\n");
      return;
    }
    gst_print ("Received OFFER_REQUEST, sending offer\n");
    /* Peer wants us to start negotiation (exchange SDP and ICE candidates) */
    if (!start_pipeline(TRUE))
      cleanup_and_quit_loop ("ERROR: failed to start pipeline",
          PEER_CALL_ERROR);
  } else if (g_str_has_prefix (text, "ERROR")) {
    /* Handle errors */
    switch (app_state) {
      case SERVER_CONNECTING:
        app_state = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        app_state = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        app_state = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        app_state = PEER_CALL_ERROR;
        break;
      default:
        app_state = APP_STATE_ERROR;
    }
    cleanup_and_quit_loop (text, APP_STATE_UNKNOWN);
  } else {
    /* Look for JSON messages containing SDP and ICE candidates */
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, text, -1, nullptr)) {
      gst_printerr ("Unknown message '%s', ignoring\n", text);
      g_object_unref (parser);
      return;
    }

    auto root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
      gst_printerr ("Unknown json message '%s', ignoring\n", text);
      g_object_unref (parser);
      return;
    }

    /* If peer connection wasn't made yet and we are expecting peer will
     * connect to us, launch pipeline at this moment */
    if (!webrtc1 && !signaling_connection->we_create_offer()) {
      if (!start_pipeline (FALSE)) {
        cleanup_and_quit_loop ("ERROR: failed to start pipeline",
            PEER_CALL_ERROR);
      }

      app_state = PEER_CALL_NEGOTIATING;
    }

    auto object = json_node_get_object (root);
    /* Check type of JSON message */
    if (json_object_has_member (object, "sdp")) {
      g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

      auto child = json_object_get_object_member (object, "sdp");

      if (!json_object_has_member (child, "type")) {
        cleanup_and_quit_loop ("ERROR: received SDP without 'type'",
            PEER_CALL_ERROR);
        return;
      }

      auto sdptype = json_object_get_string_member (child, "type");
      /* In this example, we create the offer and receive one answer by default,
       * but it's possible to comment out the offer creation and wait for an offer
       * instead, so we handle either here.
       *
       * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for another
       * example how to handle offers from peers and reply with answers using webrtcbin. */
      text = json_object_get_string_member (child, "sdp");
      GstSDPMessage *sdp;
      auto ret = gst_sdp_message_new (&sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);
      ret = gst_sdp_message_parse_buffer ((guint8 *) text, (guint) strlen(text), sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      if (g_str_equal (sdptype, "answer")) {
        gst_print ("Received answer:\n%s\n", text);
        auto answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
            sdp);
        g_assert_nonnull (answer);

        /* Set remote description on our pipeline */
        {
          GstPromise *promise = gst_promise_new ();
          g_signal_emit_by_name (webrtc1, "set-remote-description", answer,
              promise);
          gst_promise_interrupt (promise);
          gst_promise_unref (promise);
        }
        app_state = PEER_CALL_STARTED;
      } else {
        gst_print ("Received offer:\n%s\n", text);
        on_offer_received (sdp);
      }

    } else if (json_object_has_member (object, "ice")) {
        auto candidates = json_object_get_array_member(object, "ice");
        for (auto v = json_array_get_elements(candidates); v != nullptr; v = v->next)
        {
            auto child = json_node_get_object(static_cast<JsonNode*>(v->data));
            auto candidate = json_object_get_string_member(child, "candidate");
            auto sdpmlineindex = (guint)json_object_get_int_member(child, "sdpMLineIndex");

            /* Add ice candidate sent by remote peer */
            g_signal_emit_by_name(webrtc1, "add-ice-candidate", sdpmlineindex,
                candidate);
        }
    } else {
      gst_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

}


static gpointer glibMainLoopThreadFunc(gpointer data)
{
    auto self = static_cast<SendRecv *>(data);

    self->signaling_connection = get_signaling_connection();

    self->loop = g_main_loop_new(nullptr, false);

    self->signaling_connection->connect_to_server_async();

    g_main_loop_run(self->loop);
    if (self->loop)
        g_clear_pointer(&self->loop, g_main_loop_unref);
    self->loop = nullptr;

    if (self->webrtcbin_get_stats_id)
        g_source_remove(self->webrtcbin_get_stats_id);
    self->webrtcbin_get_stats_id = 0;

    if (self->pipe1) {
      gst_element_set_state (GST_ELEMENT (self->pipe1), GST_STATE_NULL);
      gst_print ("Pipeline stopped\n");
      gst_object_unref (self->pipe1);
      self->pipe1 = nullptr;
    }
    self->webrtc1 = nullptr;

    self->signaling_connection.reset();

    self->ice_candidates.clear();

    return nullptr;
}


bool start_sendrecv(unsigned long long winid, QSlider* volume_notifier, ISendRecv* sendrecv)
{
    if (loop == nullptr) {
        if (!check_plugins ())
            return false;

        xwinid = winid;

        p_sendrecv = sendrecv;

        g_volume_notifier = volume_notifier;

        app_state = APP_STATE_UNKNOWN;

        gthread = g_thread_new(nullptr, glibMainLoopThreadFunc, this);
    }

    return true;
}

}; // class SendRecv


static SendRecv sendrecv;

void cleanup_and_quit_loop(const gchar* msg, bool is_error)
{
    sendrecv.cleanup_and_quit_loop(msg, is_error);
}

bool set_connected()
{
    return sendrecv.set_connected();
}

void on_server_message(const gchar* text)
{
    sendrecv.on_server_message(text);
}

gboolean start_pipeline(gboolean create_offer)
{
    return sendrecv.start_pipeline(create_offer);
}


bool start_sendrecv(unsigned long long winid, QSlider* volume_notifier, ISendRecv* isendrecv)
{
    return sendrecv.start_sendrecv(winid, volume_notifier, isendrecv);
}
