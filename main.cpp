#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>

#include <gst/gst.h>

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
    MainWindow w;
    w.show();
    return QApplication::exec();
}
