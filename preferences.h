#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "cameraman.h"

#include <QDialog>

namespace Ui {
class Preferences;
}

class Preferences : public QDialog
{
    Q_OBJECT

public:
    explicit Preferences(QWidget *parent = nullptr);
    ~Preferences();

private slots:
    void on_pushButton_update_camera_list_clicked();
    void on_comboBox_camera_currentIndexChanged(int index);

private:
    QStringList updateCameraInfo();

private:
    Ui::Preferences *ui;
    std::vector<CameraDesc> mCameras;
};

#endif // PREFERENCES_H
