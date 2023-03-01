#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("Fusion"));
    /* Apply stylesheet */
    QFile css_data(":/style.css");
    if (css_data.open(QIODevice::ReadOnly))
    {
        a.setStyleSheet(css_data.readAll());
        css_data.close();
    }
    MainWindow w;
    w.show();
    return a.exec();
}
