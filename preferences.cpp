#include "preferences.h"
#include "ui_preferences.h"

#include "globals.h"

#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>


namespace {

const int sliceIntervalValues[] = { 1, 2, 5, 10, 30 };

void InitSliceDurationsCombo(QComboBox* combo)
{
    combo->addItem(QObject::tr("No slice"));
    for (int minutes = 0; minutes <= 1; ++minutes)
    { 
        auto templ = QObject::tr(minutes ? "%1 min" : "%1 sec");
        for (auto v : sliceIntervalValues)
        {
            combo->addItem(templ.arg(v));
        }
    }
}

int getSliceDurationSecs(QComboBox* combo) // zero if no slices
{
    const int index = combo->currentIndex() - 1;
    if (index < 0)
        return 0;

    const int minutes = index / std::size(sliceIntervalValues);
    const int valueIdx = index % std::size(sliceIntervalValues);

    return (minutes ? 60 : 1) * sliceIntervalValues[valueIdx];
}

}


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

    InitSliceDurationsCombo(ui->comboBox_SliceDuration);

    QApplication::setOverrideCursor(Qt::WaitCursor);

    ui->comboBox_camera->addItems(updateCameraInfo());
    ui->comboBox_audio->addItems(updateAudioInfo());

    QApplication::restoreOverrideCursor();

    QSettings settings;

    ui->lineEdit_SessionID->setText(settings.value(SETTING_SESSION_ID).toString());

    ui->checkBox_TURN->setChecked(settings.value(SETTING_USE_TURN).toBool());
    ui->lineEdit_TURN->setText(settings.value(SETTING_TURN).toString());

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
    auto cameraId = ui->comboBox_camera->currentText();
    auto cameraRes = ui->comboBox_camera_res->currentIndex();

    ui->comboBox_camera->clear();
    ui->comboBox_camera->addItems( updateCameraInfo() );

    ui->comboBox_camera->setCurrentText(cameraId);
    if (cameraRes >= 0)
    {
        ui->comboBox_camera_res->setCurrentIndex(cameraRes);
    }
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
    auto audioId = ui->comboBox_audio->currentText();

    ui->comboBox_audio->clear();
    ui->comboBox_audio->addItems( updateAudioInfo() );

    ui->comboBox_audio->setCurrentText(audioId);
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

void Preferences::on_checkBox_save_clicked(bool checked)
{
    if (checked)
    {
        QString folderPath = ui->lineEdit_SavePath->text().trimmed();
        if (!folderPath.isEmpty())
        {
            QDir dir(folderPath);
            if (dir.mkpath(".")) // https://stackoverflow.com/a/11517874/10472202
            {
                ui->lineEdit_SavePath->setEnabled(false);
                ui->pushButton_chooseSavePath->setEnabled(false);
                return;
            }
        }
        ui->checkBox_save->setChecked(false);
        QMessageBox::warning(this, tr("Wrong Directory path"),
            tr("Cannot use directory path: \"%1\"").arg(folderPath));
    }
    else
    {
        ui->lineEdit_SavePath->setEnabled(true);
        ui->pushButton_chooseSavePath->setEnabled(true);
        //m_videoSaver->onVideoStopped();
    }
}

void Preferences::on_pushButton_chooseSavePath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a Save Videos Directory"));
    if (!dir.isEmpty())
    {
        ui->lineEdit_SavePath->setText(dir);
    }
}

void Preferences::accept()
{
    QSettings settings;

    settings.setValue(SETTING_SESSION_ID, ui->lineEdit_SessionID->text());

    settings.setValue(SETTING_USE_TURN, ui->checkBox_TURN->isChecked());
    settings.setValue(SETTING_TURN, ui->lineEdit_TURN->text());

    settings.setValue(SETTING_AUTOVIDEOSRC, ui->autoVideoSrc->isChecked());
    settings.setValue(SETTING_AUTOAUDIOSRC, ui->autoAudioSrc->isChecked());

    settings.setValue(SETTING_CAMERA_ID, ui->comboBox_camera->currentText());
    settings.setValue(SETTING_CAMERA_RESOLUTION, ui->comboBox_camera_res->currentIndex());

    settings.setValue(SETTING_AUDIO_ID, ui->comboBox_audio->currentText());

    QString videoLaunchLine = VIDEO_LAUNCH_LINE_DEFAULT;
    if (!ui->autoVideoSrc->isChecked())
    {
        const int videoIndex = ui->comboBox_camera->currentIndex();
        const int videoResIndex = ui->comboBox_camera_res->currentIndex();
        if (videoIndex >= 0 && videoResIndex >= 0)
        {
            const auto& camera = mCameras.at(videoIndex);
            const auto& mode = camera.modes.at(videoResIndex);
            videoLaunchLine = QStringLiteral(
                        "%1 ! video/x-raw,format=%2,width=%3,height=%4,framerate=%5/%6")
                    .arg(camera.launchLine).arg(mode.format).arg(mode.w).arg(mode.h).arg(mode.den).arg(mode.num);
        }
    }
    settings.setValue(SETTING_VIDEO_LAUNCH_LINE, videoLaunchLine);
    qDebug() << SETTING_VIDEO_LAUNCH_LINE << videoLaunchLine;

    QString audioLaunchLine = AUDIO_LAUNCH_LINE_DEFAULT;
    if (!ui->autoAudioSrc->isChecked())
    {
        const int audioIndex = ui->comboBox_audio->currentIndex();
        if (audioIndex >= 0)
        {
            const auto& audio = mAudios.at(audioIndex);
            audioLaunchLine = audio.launchLine;
        }
    }
    settings.setValue(SETTING_AUDIO_LAUNCH_LINE, audioLaunchLine);
    qDebug() << SETTING_AUDIO_LAUNCH_LINE << audioLaunchLine;

    QDialog::accept();
}
