#include "signaling_connection.h"

#include "sendrecv.h"

#include "http.h"

#include "globals.h"

#include "makeguard.h"

#include <json-glib/json-glib.h>

#include <crossguid/guid.hpp>

#include <QSettings>
#include <QDebug>

#include <thread>
#include <future>
#include <mutex>

#ifdef _MSC_VER
#  undef restrict
#  define restrict
#endif

#include <openssl/hmac.h>
#include <openssl/evp.h> // recommended EVP API

static std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char out[32];
    unsigned int out_len = 0;

    HMAC(EVP_sha256(),
        key.data(), static_cast<int>(key.size()),
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        out, &out_len);

    return std::string(reinterpret_cast<char*>(out), out_len);
}


static std::string sha256(const std::string& data) {
    // Compute SHA-256 digest via EVP
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx)
        return {};

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(mdctx, reinterpret_cast<const unsigned char*>(data.data()), data.size()) != 1 ||
        EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return {};
    }
    EVP_MD_CTX_free(mdctx);

    return std::string(reinterpret_cast<char*>(hash), hash_len);
}

// URL-safe Base64 (RFC 4648 #5) without padding.
// Input `bin` contains raw bytes (may have '\0' bytes).
static std::string base64url_encode(const std::string& bin) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_"; // base64url alphabet: '+' -> '-', '/' -> '_'
    size_t len = bin.size();
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < len) {
        unsigned int b0 = static_cast<unsigned char>(bin[i]);
        unsigned int b1 = static_cast<unsigned char>(bin[i + 1]);
        unsigned int b2 = static_cast<unsigned char>(bin[i + 2]);
        out.push_back(alphabet[(b0 >> 2) & 0x3F]);
        out.push_back(alphabet[((b0 & 0x3) << 4) | ((b1 >> 4) & 0xF)]);
        out.push_back(alphabet[((b1 & 0xF) << 2) | ((b2 >> 6) & 0x3)]);
        out.push_back(alphabet[b2 & 0x3F]);
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1) {
        unsigned int b0 = static_cast<unsigned char>(bin[i]);
        out.push_back(alphabet[(b0 >> 2) & 0x3F]);
        out.push_back(alphabet[((b0 & 0x3) << 4) & 0x3F]);
        // no padding
    }
    else if (rem == 2) {
        unsigned int b0 = static_cast<unsigned char>(bin[i]);
        unsigned int b1 = static_cast<unsigned char>(bin[i + 1]);
        out.push_back(alphabet[(b0 >> 2) & 0x3F]);
        out.push_back(alphabet[((b0 & 0x3) << 4) | ((b1 >> 4) & 0xF)]);
        out.push_back(alphabet[((b1 & 0xF) << 2) & 0x3F]);
        // no padding
    }

    return out;
}



const xg::Guid this_guid = xg::newGuid();

// Use safe URL prefix constants; we'll append hashed session id.
const char message_url_prefix[] = "https://ntfy.sh/mediaThorSR_";


class NtfySignalingConnection : public ISignalingConnection
{
public:
    NtfySignalingConnection(std::string sid) : session_id(std::move(sid)) {}
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


        std::string hashed = base64url_encode(sha256(session_id));
        std::string url = message_url_prefix + hashed;

        http(HTTP_POST, url.c_str(), nullptr, message.c_str(), message.length());
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

                            auto parser = MakeGuard(json_parser_new(), g_object_unref);
                            if (!json_parser_load_from_data(parser.get(), pData, ptrEnd - pData, nullptr)) {
                                //gst_printerr("Unknown message '%s', ignoring\n", text);
                                //g_object_unref(parser);
                                break; //goto out;
                            }

                            auto root = json_parser_get_root(parser.get());
                            if (!JSON_NODE_HOLDS_OBJECT(root)) {
                                //gst_printerr("Unknown json message '%s', ignoring\n", text);
                                //g_object_unref(parser);
                                break; //goto out;
                            }

                            auto child = json_node_get_object(root);

                            if (!json_object_has_member(child, "event")) {
                                //g_object_unref(parser);
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
                                            their_giud = xg::Guid{ sender_guid };
                                        else if (their_giud.str() != sender_guid)
                                            break; //goto out;

                                        const auto message = pos + 1;

                                        // SYN <macA>
                                        const auto is_syn = g_str_has_prefix(message, "SYN");
                                        if (is_syn) {
                                            // macA
                                            const char* macA = message + 4; // after "SYN "

                                            // expected macA
                                            std::string expectedA = base64url_encode(hmac_sha256(session_id, their_giud.str() + "A"));

                                            if (expectedA != macA) {
                                                qCritical() << "MAC A mismatch, dropping connection";
                                                requestInterrupted = true;
                                                return size * nmemb;
                                            }

                                            // ACK <macB>
                                            std::string min_id = (std::min)(this_guid.str(), their_giud.str());
                                            std::string max_id = (std::max)(this_guid.str(), their_giud.str());
                                            std::string macB = base64url_encode(hmac_sha256(session_id, min_id + max_id + "B"));
                                            std::string ack_msg = "ACK " + macB;
                                            send_text(ack_msg.data());
                                        }

                                        // ACK <macB>
                                        const auto is_ack = !is_syn && g_str_has_prefix(message, "ACK");
                                        if (is_ack) {

                                            const char* macB = message + 4;

                                            std::string min_id = (std::min)(this_guid.str(), their_giud.str());
                                            std::string max_id = (std::max)(this_guid.str(), their_giud.str());
                                            std::string expectedB = base64url_encode(hmac_sha256(session_id, min_id + max_id + "B"));

                                            if (expectedB != macB) {
                                                qCritical() << "MAC B mismatch, dropping connection";
                                                requestInterrupted = true;
                                                return size * nmemb;
                                            }
                                        }

                                        if (is_syn || is_ack) {
                                            if (set_connected()) {
                                                /* Start negotiation (exchange SDP and ICE candidates) */
                                                if (we_create_offer() && !start_pipeline(TRUE))
                                                    cleanup_and_quit_loop("ERROR: failed to start pipeline", true);
                                            }
                                        }
                                        else
                                            on_server_message(message);
                                    }
                                }
                            }
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


                // Build recv URL with hashed session id
                std::string hashed = base64url_encode(sha256(session_id));
                std::string url = message_url_prefix + hashed + "/sse";

                http(HTTP_GET, url.c_str(), headers, nullptr, 0, on_data, verify_sse_response, progress_callback);
        };

        // https://stackoverflow.com/a/23454840/10472202
        signaling_runner = std::thread(threadLam, std::move(startedPromise));

        if (!startedResult.get())
            return false;

        std::string macA = base64url_encode(hmac_sha256(session_id, this_guid.str() + "A"));

        std::string syn_msg = "SYN " + macA;
        send_text(syn_msg.data());

        return true;
    }

    void close() override
    {
        doClose();
    }
    void doClose()
    {
        requestInterrupted = true;
        if (signaling_runner.get_id() != std::this_thread::get_id())
        {
            std::call_once(once_closed, [this] {
                if (signaling_runner.joinable())
                {
                    signaling_runner.join();
                }
                });
        }
    }

private:
    xg::Guid their_giud;

    std::thread signaling_runner;

    std::atomic_bool requestInterrupted = false;
    std::once_flag once_closed;

    std::string session_id;
};

std::unique_ptr<ISignalingConnection> get_signaling_connection(std::string sid)
{
    return std::make_unique<NtfySignalingConnection>(std::move(sid));
}
