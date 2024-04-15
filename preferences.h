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

private Q_SLOTS:
    void on_pushButton_update_camera_list_clicked();
    void on_comboBox_camera_currentIndexChanged(int index);
    void on_pushButton_update_audio_list_clicked();
    void on_comboBox_audio_currentIndexChanged(int index);
    void on_checkBox_save_clicked(bool checked);
    void on_pushButton_chooseSavePath_clicked();

private:
    QStringList updateCameraInfo();
    QStringList updateAudioInfo();

    void accept() override;

private:
    Ui::Preferences *ui;
    std::vector<CameraDesc> mCameras;
    std::vector<AudioDesc> mAudios;
};

#endif // PREFERENCES_H
