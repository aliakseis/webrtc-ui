#include "signaling_connection.h"

#include "sendrecv.h"

#include "http.h"

#include "globals.h"

#include <json-glib/json-glib.h>

#include <crossguid/guid.hpp>

#include <QSettings>
#include <QDebug>

#include <thread>
#include <future>
#include <mutex>

const xg::Guid this_guid = xg::newGuid();

const char send_message_url[] = "https://ntfy.sh/mediaThorSendRecv_%s";
const char recv_message_url[] = "https://ntfy.sh/mediaThorSendRecv_%s/sse";


class NtfySignalingConnection : public ISignalingConnection
{
public:
    ~NtfySignalingConnection() override
    {
        doClose();
    }
protected:
    bool we_create_offer() override
    {
        return this_guid.bytes() < their_giud.bytes();
    }

    void send_text(const char *text) override
    {
        const auto message = this_guid.str() + '\n' + text;

        char buffer[1024];
        sprintf(buffer, send_message_url, QSettings().value(SETTING_SESSION_ID).toString().toStdString().c_str());
        http(HTTP_POST, buffer, nullptr, message.c_str(), message.length());
    }

    static const char* verify_sse_response(CURL* curl) {
#define EXPECTED_CONTENT_TYPE "text/event-stream"

        static const char expected_content_type[] = EXPECTED_CONTENT_TYPE;

        const char* content_type;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
        if (!content_type) content_type = "";

        if (!strncmp(content_type, expected_content_type, strlen(expected_content_type)))
            return nullptr;

        return "Invalid content_type, should be '" EXPECTED_CONTENT_TYPE "'.";
    }

    bool connect_to_server_async() override
    {
        std::promise<bool> startedPromise;

        auto startedResult = startedPromise.get_future();

        auto threadLam = [this](
            std::promise<bool> startedPromise
            ) {
                const char* headers[] = {
                    "Accept: text/event-stream",
                    nullptr
                };

                auto on_data = [this, &startedPromise](char *ptr, size_t size, size_t nmemb)->size_t {
                    try {
                        const auto ptrEnd = ptr + size * nmemb;

                        const char watch[] = "data:";

                        auto pData = std::search(ptr, ptrEnd, std::begin(watch), std::prev(std::end(watch)));
                        if (pData != ptrEnd) do {
                            pData += sizeof(watch) / sizeof(watch[0]) - 1;

                            JsonParser *parser = json_parser_new();
                            if (!json_parser_load_from_data(parser, pData, ptrEnd - pData, nullptr)) {
                                //gst_printerr("Unknown message '%s', ignoring\n", text);
                                g_object_unref(parser);
                                break; //goto out;
                            }

                            auto root = json_parser_get_root(parser);
                            if (!JSON_NODE_HOLDS_OBJECT(root)) {
                                //gst_printerr("Unknown json message '%s', ignoring\n", text);
                                g_object_unref(parser);
                                break; //goto out;
                            }

                            auto child = json_node_get_object(root);

                            if (!json_object_has_member(child, "event")) {
                                g_object_unref(parser);
                                break; //goto out;
                            }

                            auto sdptype = json_object_get_string_member(child, "event");
                            if (g_str_equal(sdptype, "open")) {
                                startedPromise.set_value(true);
                            }
                            else if (g_str_equal(sdptype, "message")) {
                                auto text = json_object_get_string_member(child, "message");

                                if (auto pos = strchr(text, '\n'); pos != nullptr && pos != text)
                                {
                                    std::string_view sender_guid(text, pos - text);
                                    if (this_guid.str() != sender_guid)
                                    {
                                        if (!their_giud.isValid())
                                            their_giud = std::string(sender_guid);

                                        const auto message = pos + 1;
                                        const bool is_syn = g_strcmp0(message, "SYN") == 0;
                                        if (is_syn)
                                            send_text("ACK");

                                        if (is_syn || g_strcmp0(message, "ACK") == 0) {
                                            if (app_state < PEER_CONNECTED) {
                                                app_state = PEER_CONNECTED;
                                                /* Start negotiation (exchange SDP and ICE candidates) */
                                                if (we_create_offer() && !start_pipeline(TRUE))
                                                    cleanup_and_quit_loop("ERROR: failed to start pipeline",
                                                        PEER_CALL_ERROR);
                                            }
                                        }
                                        else
                                            on_server_message(message);
                                    }
                                }
                            }

                            g_object_unref(parser);

                        } while (false);
                    }
                    catch (const std::exception& ex) {
                        qCritical() << "Exception " << typeid(ex).name() << ": " << ex.what();
                        requestInterrupted = true;
                        try {
                            startedPromise.set_value(false);
                        } catch (...) {
                        }
                    }
                    return size * nmemb;
                };

                auto progress_callback = [this](curl_off_t dltotal,
                    curl_off_t dlnow,
                    curl_off_t ultotal,
                    curl_off_t ulnow)->size_t {
                        return requestInterrupted;
                };

                char buffer[1024];
                sprintf(buffer, recv_message_url, QSettings().value(SETTING_SESSION_ID).toString().toStdString().c_str());
                http(HTTP_GET, buffer, headers, nullptr, 0, on_data, verify_sse_response, progress_callback);
        };

        // https://stackoverflow.com/a/23454840/10472202
        signaling_runner = std::thread(threadLam, std::move(startedPromise));

        if (!startedResult.get())
            return false;

        send_text("SYN");

        return true;
    }

    void close() override
    {
        doClose();
    }
    void doClose()
    {
        std::call_once(once_closed, [this] {
            if (signaling_runner.joinable())
            {
                requestInterrupted = true;
                signaling_runner.join();
            }
        });
    }

private:
    xg::Guid their_giud;

    std::thread signaling_runner;

    std::atomic_bool requestInterrupted = false;
    std::once_flag once_closed;
};

std::unique_ptr<ISignalingConnection> get_signaling_connection()
{
    return std::make_unique<NtfySignalingConnection>();
}
