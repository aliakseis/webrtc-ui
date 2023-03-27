#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QSettings>

#include <gst/gst.h>

#include "preferences.h"
#include "globals.h"

int main(int argc, char *argv[])
{
    gst_init( &argc, &argv );

    QApplication a(argc, argv);

    QApplication::setApplicationName("webrtc-ui");
    QApplication::setOrganizationName("aliakseis");
    QApplication::setOrganizationDomain("github.com");

    QApplication::setStyle(QStyleFactory::create("Fusion"));
    /* Apply stylesheet */
    QFile css_data(":/style.css");
    if (css_data.open(QIODevice::ReadOnly))
    {
        a.setStyleSheet(css_data.readAll());
        css_data.close();
    }

    if (QSettings().value(SETTING_SESSION_ID).toString().trimmed().isEmpty())
    {
        Preferences prefDlg(nullptr);
        if (prefDlg.exec() != QDialog::Accepted)
            return 1;
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
