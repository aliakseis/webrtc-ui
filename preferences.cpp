#include "preferences.h"
#include "ui_preferences.h"

Preferences::Preferences(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Preferences)
{
    ui->setupUi(this);

    on_pushButton_update_camera_list_clicked();
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
