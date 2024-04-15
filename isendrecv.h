#pragma once

#include <functional>

#include <QMetaObject>
#include <QString>

struct ISendRecv
{
    virtual void handleRecv(uintptr_t id, const char* data) = 0;
    virtual QMetaObject::Connection setAudioVolumeLambda(std::function<void(int)> lambda) = 0;
    virtual QMetaObject::Connection setSendLambda(std::function<void(const QString&)> lambda) = 0;
    virtual void onQuit() = 0;
};
