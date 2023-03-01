#pragma once

#include <QString>
#include <vector>

struct CameraMode
{
    int w;
    int h;
    int num;
    int den;

    QString format;

    double fps() const;
    QString getDescr() const;
};
    
struct CameraDesc
{
    QString id;
    QString description;
    QString launchLine;
    std::vector<CameraMode> modes;
};

std::vector<CameraDesc> getCameraDescriptions();

