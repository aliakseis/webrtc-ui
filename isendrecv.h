#pragma once

#include <functional>

#include <QString>

struct ISendRecv
{
    virtual void handleRecv(const char* data) = 0;
    virtual void setSendLambda(std::function<void(const QString&)> lambda) = 0;
    virtual void onQuit() = 0;
};
