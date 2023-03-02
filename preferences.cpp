#include "preferences.h"
#include "ui_preferences.h"

#include <QSettings>

const auto SETTING_SESSION_ID = QStringLiteral("sessionId");

const auto SETTING_AUTOVIDEOSRC = QStringLiteral("autovideosrc");
const auto SETTING_AUTOAUDIOSRC = QStringLiteral("autoaudiosrc");

const auto SETTING_CAMERA_ID = QStringLiteral("cameraId");
const auto SETTING_CAMERA_RESOLUTION = QStringLiteral("cameraResolution");

const auto SETTING_AUDIO_ID = QStringLiteral("audioId");

Preferences::Preferences(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Preferences)
{
    ui->setupUi(this);

    on_pushButton_update_camera_list_clicked();
    on_pushButton_update_audio_list_clicked();

    QSettings settings;

    ui->lineEdit_SessionID->setText(settings.value(SETTING_SESSION_ID).toString());

    (settings.value(SETTING_AUTOVIDEOSRC, true).toBool()
        ? ui->autoVideoSrc : ui->gstreamerSource)->setChecked(true);

    (settings.value(SETTING_AUTOAUDIOSRC, true).toBool()
        ? ui->autoAudioSrc : ui->gstreamerAudioSource)->setChecked(true);

    if (auto cameraId = settings.value(SETTING_CAMERA_ID); cameraId.isValid())
    {
        ui->comboBox_camera->setCurrentText(cameraId.toString());
        if (auto cameraRes = settings.value(SETTING_CAMERA_RESOLUTION); cameraRes.isValid())
        {
            ui->comboBox_camera_res->setCurrentIndex(cameraRes.toInt());
        }
    }
    if (auto audioId = settings.value(SETTING_AUDIO_ID); audioId.isValid())
    {
        ui->comboBox_audio->setCurrentText(audioId.toString());
    }
}

Preferences::~Preferences()
{
    delete ui;
}


QStringList Preferences::updateCameraInfo()
{
    QStringList res;

    mCameras = getCameraDescriptions();
    for (const auto& cameraInfo : mCameras)
    {
        res.push_back(cameraInfo.id);
    }

    return res;
}

QStringList Preferences::updateAudioInfo()
{
    QStringList res;

    mAudios = getAudioDescriptions();
    for (const auto& audioInfo : mAudios)
    {
        res.push_back(audioInfo.id);
    }

    return res;
}


void Preferences::on_pushButton_update_camera_list_clicked()
{
    ui->comboBox_camera->clear();
    ui->comboBox_camera->addItems( updateCameraInfo() );
}

void Preferences::on_comboBox_camera_currentIndexChanged(int index)
{
    if( mCameras.empty() ) {
        return;
    }

    if( index>mCameras.size()-1  ) {
        return;
    }

    if( index<0 )
    {
        ui->label_camera->setText( tr("No camera info") );
    }
    else
    {
        ui->label_camera->setText(mCameras.at(index).description);

        ui->comboBox_camera_res->clear();

        for (const auto& mode : mCameras[index].modes)
        {
            ui->comboBox_camera_res->addItem(mode.getDescr());
        }
    }
}

void Preferences::on_pushButton_update_audio_list_clicked()
{
    ui->comboBox_audio->clear();
    ui->comboBox_audio->addItems( updateAudioInfo() );
}

void Preferences::on_comboBox_audio_currentIndexChanged(int index)
{
    if( mAudios.empty() ) {
        return;
    }

    if( index>mAudios.size()-1  ) {
        return;
    }

    if( index<0 )
    {
        ui->label_audio->setText( tr("No audio info") );
    }
    else
    {
        ui->label_audio->setText(mAudios.at(index).description);
    }
}

void Preferences::accept()
{
    QSettings settings;

    settings.setValue(SETTING_SESSION_ID, ui->lineEdit_SessionID->text());

    settings.setValue(SETTING_AUTOVIDEOSRC, ui->autoVideoSrc->isChecked());
    settings.setValue(SETTING_AUTOAUDIOSRC, ui->autoAudioSrc->isChecked());

    settings.setValue(SETTING_CAMERA_ID, ui->comboBox_camera->currentText());
    settings.setValue(SETTING_CAMERA_RESOLUTION, ui->comboBox_camera->currentIndex());

    settings.setValue(SETTING_AUDIO_ID, ui->comboBox_audio->currentText());

    QDialog::accept();
}
