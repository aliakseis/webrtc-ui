#pragma once

#include <memory>

struct ISignalingConnection {
    virtual ~ISignalingConnection() {};
    virtual bool connect_to_server_async() = 0;
    virtual bool we_create_offer() = 0;
    virtual void send_text(const char *text) = 0;
    virtual void close() = 0;
};

std::unique_ptr<ISignalingConnection> get_signaling_connection();
