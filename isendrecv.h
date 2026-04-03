#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct ISendRecv
{
    // Register a callback used to send an outgoing text message.
    // The implementation should return an unregister function (callable with no args)
    // which, when invoked, will unregister the provided callback.
    //
    // The caller will take that returned unregister function, heap-allocate it,
    // and pass it to GLib as user-data; GLib will call it (and delete it) when
    // the associated GObject is destroyed.
    virtual std::function<void()> setSendLambda(std::function<void(const std::string&)> sendFn) = 0;

    // Register a callback to set audio volume (0..100). Returns an unregister function.
    virtual std::function<void()> setAudioVolumeLambda(std::function<void(int)> volumeFn) = 0;

    // Called when the sendrecv logic is quitting.
    virtual void onQuit() = 0;

    // Called when a text message arrives on a data channel.
    // 'channel' is an opaque pointer value (cast to uintptr_t previously).
    virtual void handleRecv(uintptr_t channel, const char* text) = 0;
};
