#include "sendrecv.h"
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

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1 = nullptr;
static GObject *send_channel, *receive_channel;

AppState app_state = APP_STATE_UNKNOWN;

static guint webrtcbin_get_stats_id = 0;

const static gboolean remote_is_offerer = FALSE;

static std::vector<std::pair<int, std::string>> ice_candidates;

static guintptr xwinid;

static QSlider* g_volume_notifier;

////////////////////////////////////////////////////////////////////


static std::unique_ptr<ISignalingConnection> signaling_connection;


////////////////////////////////////////////////////////////////////


gboolean
cleanup_and_quit_loop (const gchar * msg, enum AppState state)
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

  /* To allow usage as a GSourceFunc */
  return G_SOURCE_REMOVE;
}

static gchar *
get_string_from_json_object (JsonObject * object)
{
  JsonNode *root;
  JsonGenerator *generator;
  gchar *text;

  /* Make it the root node */
  root = json_node_init_object (json_node_alloc (), object);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, nullptr);

  /* Release everything */
  g_object_unref (generator);
  json_node_free (root);
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
static GstElement *
find_video_sink ()
{
  GstStateChangeReturn sret;
  GstElement *sink;

  if ((sink = gst_element_factory_make ("xvimagesink", nullptr))) {
    sret = gst_element_set_state (sink, GST_STATE_READY);
    if (sret == GST_STATE_CHANGE_SUCCESS)
      return sink;

    gst_element_set_state (sink, GST_STATE_NULL);
    gst_object_unref (sink);
  }

  if ((sink = gst_element_factory_make ("ximagesink", nullptr))) {
    sret = gst_element_set_state (sink, GST_STATE_READY);
    if (sret == GST_STATE_CHANGE_SUCCESS)
      return sink;

    gst_element_set_state (sink, GST_STATE_NULL);
    gst_object_unref (sink);
  }

  if (strcmp (DEFAULT_VIDEOSINK, "xvimagesink") == 0 ||
      strcmp (DEFAULT_VIDEOSINK, "ximagesink") == 0)
    return nullptr;

  if ((sink = gst_element_factory_make (DEFAULT_VIDEOSINK, nullptr))) {
    if (GST_IS_BIN (sink)) {
      gst_object_unref (sink);
      return nullptr;
    }

    sret = gst_element_set_state (sink, GST_STATE_READY);
    if (sret == GST_STATE_CHANGE_SUCCESS)
      return sink;

    gst_element_set_state (sink, GST_STATE_NULL);
    gst_object_unref (sink);
  }

  return nullptr;
}


static GstPadProbeReturn
static_rtp_packet_loss_probe(GstPad *opad, GstPadProbeInfo *p_info, gpointer /*p_data*/)
{
    if (G_UNLIKELY((p_info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) == 0))
        return GST_PAD_PROBE_OK;

    GstEvent *event = gst_pad_probe_info_get_event(p_info);
    switch (GST_EVENT_TYPE(event))
    {
    //case GST_EVENT_CUSTOM_DOWNSTREAM:
    //{
    //    // rtpjitterbuffer (which is inside the rtpbin
    //    // that is inside rtspsrc) generates this event.
    //    // So far, there is no dedicated packet loss event
    //    // type, and custom downstream events are used instead.
    //    if (gst_event_has_name(event, "GstRTPPacketLost"))
    //    {
    //        GstStructure const *s = gst_event_get_structure(event);
    //        guint num_packets = 1;

    //        if (gst_structure_has_field(s, "num-packets"))
    //            gst_structure_get_uint(s, "num-packets", &num_packets);

    //        gst_print("detected %d lost or too-late packet(s)", num_packets);
    //    }

    //    break;
    //}

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
    GObject *where_the_object_was)
{
    auto c = static_cast<QMetaObject::Connection*>(data);
    auto ok = QObject::disconnect(*c);
    g_assert_true(ok);
    delete c;
}

static void
handle_media_stream (GstPad * pad, GstElement * pipe, const char *convert_name,
    //const char *sink_name)
                     GstElement* sink)
{
  //gst_println ("Trying to handle stream with %s ! %s", convert_name, sink_name);

  auto q = gst_element_factory_make ("queue", nullptr);
  g_assert_nonnull (q);
  auto conv = gst_element_factory_make (convert_name, nullptr);
  g_assert_nonnull (conv);
  //sink = gst_element_factory_make (sink_name, nullptr);
  g_assert_nonnull (sink);

  if (g_strcmp0 (convert_name, "audioconvert") == 0) {
    /* Might also need to resample, so add it just in case.
     * Will be a no-op if it's not required. */
    auto resample = gst_element_factory_make ("audioresample", nullptr);
    g_assert_nonnull (resample);


    auto volume = gst_element_factory_make("volume", nullptr);
    auto c = new QMetaObject::Connection(
        QObject::connect(g_volume_notifier, &QSlider::valueChanged, [volume](int v) {
                g_object_set(volume, "volume", v / 100., NULL);
            })
    );
    g_object_weak_ref(G_OBJECT(volume), disconnect, c);

    gst_bin_add_many (GST_BIN (pipe), q, conv, resample, volume, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (resample);
    gst_element_sync_state_with_parent(volume);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, resample, volume, sink, NULL);
  } else {
      // adding a probe for handling loss messages from rtpbin
      gst_pad_add_probe(pad,
          GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
          static_rtp_packet_loss_probe,
          nullptr,
          nullptr);

    gst_bin_add_many (GST_BIN (pipe), q, conv, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, sink, NULL);
  }

  auto qpad = gst_element_get_static_pad (q, "sink");

  auto ret = gst_pad_link (pad, qpad);
  g_assert_cmphex (ret, ==, GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    GstElement * pipe)
{
  GstCaps *caps;
  const gchar *name;

  if (!gst_pad_has_current_caps (pad)) {
    gst_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
        GST_PAD_NAME (pad));
    return;
  }

  caps = gst_pad_get_current_caps (pad);
  name = gst_structure_get_name (gst_caps_get_structure (caps, 0));

  auto str = gst_caps_to_string(caps);
  g_print("on_incoming_decodebin_stream pad caps: %s\n", str);
  g_free(str);

  if (g_str_has_prefix (name, "video")) {
    auto sink = find_video_sink();
    handle_media_stream (pad, pipe, "videoconvert", sink);//"autovideosink");
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink), xwinid);
  } else if (g_str_has_prefix (name, "audio")) {
    handle_media_stream (pad, pipe, "audioconvert", gst_element_factory_make("autoaudiosink", nullptr));
  } else {
    gst_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }
}


static auto prepare_next_file_name() {
    QDateTime now = QDateTime::currentDateTime();
    const auto name = now.toString("yyMMddhhmmss");
    const auto path = QSettings().value(SETTING_SAVE_PATH).toString() + '/' + name + ".webm";
    return QFile::encodeName(path);// .constData();
}

gchar *splitmuxsink_on_format_location_full(GstElement *splitmux,
    guint fragment_id,
    GstSample *first_sample,
    gpointer user_data) {
    //g_print("Requesting new file path for device recording \n");
    auto nextfilename = prepare_next_file_name();
    g_print("New file name generated for recording as %s \n", nextfilename.constData());
    return g_strdup_printf("%s", nextfilename.constData());
}

static GstElement* get_file_sink(GstBin* pipe)
{
    static std::mutex mtx;
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
        //auto muxer = gst_element_factory_make("webmmux", nullptr);
        g_object_set(G_OBJECT(splitmuxsink),
            //"location", "d:/videos/video%05d.webm",
            "async-finalize", TRUE,
            "max-size-time", GST_SECOND * sliceDurationSecs,  //(guint64)10000000000,
            "muxer-factory", muxerName,
            "muxer-properties", s,
            //"muxer", muxer,
            NULL);
        g_signal_connect(splitmuxsink, "format-location-full",
            G_CALLBACK(splitmuxsink_on_format_location_full), NULL);

        auto ok = gst_bin_add(GST_BIN(pipe1), splitmuxsink);
        g_assert_true(ok);

        ok = gst_element_sync_state_with_parent(splitmuxsink);
        g_assert_true(ok);

        return splitmuxsink;
    }
    
    auto muxer = gst_element_factory_make(muxerName, file_sink_name);
    auto ok = gst_bin_add(GST_BIN(pipe1), muxer);
    g_assert_true(ok);

    ok = gst_element_sync_state_with_parent(muxer);
    g_assert_true(ok);

    auto filesink = gst_element_factory_make("filesink", nullptr);
    ok = gst_bin_add(GST_BIN(pipe1), filesink);
    g_assert_true(ok);

    auto nextfilename = prepare_next_file_name();
    g_object_set(G_OBJECT(filesink),
        "location", nextfilename.constData(),
        NULL);

    ok = gst_element_sync_state_with_parent(filesink);
    g_assert_true(ok);

    ok = gst_element_link_many(
        //tee,
        //rtpvp8depay,
        muxer,
        filesink,
        NULL);
    g_assert_true(ok);

    return muxer;
}

static GstClockTime last_video_pts{};

// https://stackoverflow.com/questions/29107370/gstreamer-timestamps-pts-are-not-monotonically-increasing-for-captured-frames
static GstPadProbeReturn
gst_pad_probe_callback(GstPad * pad,
    GstPadProbeInfo * info,
    gpointer user_data)
{

    auto buffer = gst_pad_probe_info_get_buffer(info);

    auto pts = buffer->pts;
    if (pts <= last_video_pts)
    {
        g_print("Out-of-order pts: %lld; the last pts: %lld.\n", pts, last_video_pts);
        return GST_PAD_PROBE_DROP;
    }
    last_video_pts = pts;

    return GST_PAD_PROBE_OK;
}

static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  auto decodebin = gst_element_factory_make ("decodebin", nullptr);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), pipe);
  gst_bin_add (GST_BIN (pipe), decodebin);
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
      auto tee = gst_element_factory_make("tee", nullptr);// "tee");
      gst_bin_add(GST_BIN(pipe), tee);
      gst_element_sync_state_with_parent(tee);

      {
          auto sinkpad = gst_element_get_static_pad(tee, "sink");
          auto ret = gst_pad_link(pad, sinkpad);
          g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
          gst_object_unref(sinkpad);
      }
      {
          auto srcpad = gst_element_get_request_pad(tee, "src_%u");
          auto sinkpad = gst_element_get_static_pad(decodebin, "sink");
          auto ret = gst_pad_link(srcpad, sinkpad);
          g_assert_cmphex(ret, == , GST_PAD_LINK_OK);
          gst_object_unref(srcpad);
          gst_object_unref(sinkpad);
      }

      auto rtpvp8depay = gst_element_factory_make(
          (payload == 96) ? "rtpvp8depay" : "rtpopusdepay",
          nullptr);// "rtpvp8depay");

      auto ok = gst_bin_add(GST_BIN(pipe1), rtpvp8depay);
      g_assert_true(ok);

      ok = gst_element_sync_state_with_parent(rtpvp8depay);
      g_assert_true(ok);

      auto queue = gst_element_factory_make("queue", nullptr);

      ok = gst_bin_add(GST_BIN(pipe1), queue);
      g_assert_true(ok);

      ok = gst_element_sync_state_with_parent(queue);
      g_assert_true(ok);

      auto sink = get_file_sink(GST_BIN(pipe1));

      if (payload == 96)
      {
        last_video_pts = {};

        //auto identity = gst_element_factory_make("identity", nullptr);

        //auto ok = gst_bin_add(GST_BIN(pipe1), identity);
        //g_assert_true(ok);

        //ok = gst_element_sync_state_with_parent(identity);
        //g_assert_true(ok);

        auto srcpad = gst_element_get_static_pad(rtpvp8depay, "src");
        gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, gst_pad_probe_callback, nullptr, nullptr);
      }

      ok = gst_element_link_many(tee,
              //rtpjitterbuffer,
              rtpvp8depay,
              queue,
              //sink,
              NULL);
      g_assert_true(ok);

      auto srcpad = gst_element_get_static_pad(queue, "src");
      auto sinkpad = gst_element_get_request_pad(sink, 
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

#if 0
static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, gpointer user_data G_GNUC_UNUSED)
{
  if (app_state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send ICE, not in call", APP_STATE_ERROR);
    return;
  }

  auto ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  auto msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  auto text = get_string_from_json_object (msg);
  json_object_unref (msg);

  signaling_connection->send_text(text);
  g_free (text);
}
#endif

static void
send_ice_candidate_message(GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, gpointer user_data G_GNUC_UNUSED)
{
    ice_candidates.emplace_back( mlineindex, candidate );
}


static void
send_candidates()
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


static void
send_sdp_to_peer (GstWebRTCSessionDescription * desc)
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
  GstWebRTCSessionDescription *offer = nullptr;
  const GstStructure *reply;

  g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_sdp_to_peer (offer);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, gpointer user_data)
{
  gboolean create_offer = GPOINTER_TO_INT (user_data);
  app_state = PEER_CALL_NEGOTIATING;

  if (remote_is_offerer) {
      signaling_connection->send_text("OFFER_REQUEST");
  } else if (create_offer) {
    GstPromise *promise =
        gst_promise_new_with_change_func (on_offer_created, nullptr, nullptr);
    g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
  }
}

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

static void
data_channel_on_error (GObject * dc, gpointer user_data)
{
  cleanup_and_quit_loop ("Data channel error", APP_STATE_UNKNOWN);
}

static void
data_channel_on_open (GObject * dc, gpointer user_data)
{
  GBytes *bytes = g_bytes_new ("data", strlen ("data"));
  gst_print ("data channel opened\n");
  g_signal_emit_by_name (dc, "send-string", "Hi! from GStreamer");
  g_signal_emit_by_name (dc, "send-data", bytes);
  g_bytes_unref (bytes);
}

static void
data_channel_on_close (GObject * dc, gpointer user_data)
{
  cleanup_and_quit_loop ("Data channel closed", APP_STATE_UNKNOWN);
}

static void
data_channel_on_message_string (GObject * dc, gchar * str, gpointer user_data)
{
  gst_print ("Received data channel message: %s\n", str);
}

static void
connect_data_channel_signals (GObject * data_channel)
{
  g_signal_connect (data_channel, "on-error",
      G_CALLBACK (data_channel_on_error), NULL);
  g_signal_connect (data_channel, "on-open", G_CALLBACK (data_channel_on_open),
      NULL);
  g_signal_connect (data_channel, "on-close",
      G_CALLBACK (data_channel_on_close), NULL);
  g_signal_connect (data_channel, "on-message-string",
      G_CALLBACK (data_channel_on_message_string), NULL);
}

static void
on_data_channel (GstElement * webrtc, GObject * data_channel,
    gpointer user_data)
{
  connect_data_channel_signals (data_channel);
  receive_channel = data_channel;
}

static void
on_ice_gathering_state_notify (GstElement * webrtcbin, GParamSpec * pspec,
    gpointer user_data)
{
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
      send_candidates();
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

static gboolean webrtcbin_get_stats (GstElement * webrtcbin);

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
on_webrtcbin_get_stats (GstPromise * promise, GstElement * webrtcbin)
{
  const GstStructure *stats;

  g_return_if_fail (gst_promise_wait (promise) == GST_PROMISE_RESULT_REPLIED);

  stats = gst_promise_get_reply (promise);
  gst_structure_foreach (stats, on_webrtcbin_stat, nullptr);

  webrtcbin_get_stats_id = g_timeout_add (100, (GSourceFunc) webrtcbin_get_stats, webrtcbin);
}

static gboolean
webrtcbin_get_stats (GstElement * webrtcbin)
{
  GstPromise *promise;

  promise =
      gst_promise_new_with_change_func (
      (GstPromiseChangeFunc) on_webrtcbin_get_stats, webrtcbin, nullptr);

  GST_TRACE ("emitting get-stats on %" GST_PTR_FORMAT, webrtcbin);
  g_signal_emit_by_name (webrtcbin, "get-stats", NULL, promise);
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

    //    if (context->loop)
    //        g_main_loop_quit(context->loop);

    //    return FALSE;
    }

    //case GST_MESSAGE_EOS:
    //{
    //    g_message("End-of-stream");
    //    if (context->loop)
    //        g_main_loop_quit(context->loop);
    //    break;
    //}

    case GST_MESSAGE_LATENCY:
    {
        // when pipeline latency is changed, this msg is posted on the bus. we then have
        // to explicitly tell the pipeline to recalculate its latency
        if (pipe1 && !gst_bin_recalculate_latency(GST_BIN(pipe1)))
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

  pipe1 =
      gst_parse_launch (("webrtcbin bundle-policy=max-bundle name=sendrecv "
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
      "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. ").c_str(), &error);

  if (error) {
    gst_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  // add bus call
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe1));
  gst_bus_add_watch(bus, bus_call, nullptr);
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

    auto lam = [] (const gchar* name) {
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
      G_CALLBACK (on_negotiation_needed), GINT_TO_POINTER (create_offer));
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc1, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), NULL);
  g_signal_connect (webrtc1, "notify::ice-gathering-state",
      G_CALLBACK (on_ice_gathering_state_notify), NULL);
  g_signal_connect(webrtc1, "on-new-transceiver",
      G_CALLBACK(on_new_transceiver), NULL);
  g_signal_connect(webrtc1, "notify::ice-connection-state",
      G_CALLBACK(on_ice_connection_state_notify), NULL);

  /*
  auto rtpbin = gst_bin_get_by_name(GST_BIN(webrtc1), "rtpbin");
  g_object_set(rtpbin, "latency", 1000, NULL);
  g_object_unref(rtpbin);
  */

  gst_element_set_state (pipe1, GST_STATE_READY);

  g_signal_emit_by_name (webrtc1, "create-data-channel", "channel", NULL,
      &send_channel);
  if (send_channel) {
    gst_print ("Created data channel\n");
    connect_data_channel_signals (send_channel);
  } else {
    gst_print ("Could not create data channel, is usrsctp available?\n");
  }

  g_signal_connect (webrtc1, "on-data-channel", G_CALLBACK (on_data_channel),
      NULL);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream),
      pipe1);
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1);

  webrtcbin_get_stats_id = g_timeout_add (100, (GSourceFunc) webrtcbin_get_stats, webrtc1);

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
  GstWebRTCSessionDescription *answer = nullptr;
  const GstStructure *reply;

  g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1, "set-local-description", answer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send answer to peer */
  send_sdp_to_peer (answer);
  gst_webrtc_session_description_free (answer);
}

static void
on_offer_set (GstPromise * promise, gpointer user_data)
{
  gst_promise_unref (promise);
  promise = gst_promise_new_with_change_func (on_answer_created, nullptr, nullptr);
  g_signal_emit_by_name (webrtc1, "create-answer", NULL, promise);
}

static void
on_offer_received (GstSDPMessage * sdp)
{
  GstWebRTCSessionDescription *offer = nullptr;
  GstPromise *promise;

  offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  g_assert_nonnull (offer);

  /* Set remote description on our pipeline */
  {
    promise = gst_promise_new_with_change_func (on_offer_set, nullptr, nullptr);
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



static gboolean
check_plugins ()
{
  gboolean ret;
  GstPlugin *plugin;
  GstRegistry *registry;
  const gchar *needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
    "rtpmanager", "videotestsrc", "audiotestsrc", nullptr
  };

  registry = gst_registry_get ();
  ret = TRUE;
  for (guint i = 0; i < g_strv_length ((gchar **) needed); i++) {
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      gst_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
      ret = FALSE;
      continue;
    }
    gst_object_unref (plugin);
  }
  return ret;
}

//GMainLoop *gloop = 0;
GThread *gthread = nullptr;

static gpointer glibMainLoopThreadFunc(gpointer /*unused*/)
{
    signaling_connection = get_signaling_connection();

    loop = g_main_loop_new(nullptr, false);

    signaling_connection->connect_to_server_async();

    g_main_loop_run(loop);
    //g_main_loop_unref(loop);
    if (loop)
        g_clear_pointer(&loop, g_main_loop_unref);
    loop = nullptr;

    if (webrtcbin_get_stats_id)
        g_source_remove(webrtcbin_get_stats_id);
    webrtcbin_get_stats_id = 0;

    if (pipe1) {
      gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
      gst_print ("Pipeline stopped\n");
      gst_object_unref (pipe1);
      pipe1 = nullptr;
    }
    webrtc1 = nullptr;

    signaling_connection.reset();

    ice_candidates.clear();

    return nullptr;
}


bool start_sendrecv(unsigned long long winid, QSlider* volume_notifier)
{
    if (loop == nullptr) {
        if (!check_plugins ())
            return false;

        xwinid = winid;

        g_volume_notifier = volume_notifier;

        app_state = APP_STATE_UNKNOWN;

        gthread = g_thread_new(nullptr, glibMainLoopThreadFunc, nullptr);
    }

    return true;
}
