#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QSettings>
#include <QMessageBox>

#include <gst/gst.h>

#include "preferences.h"
#include "globals.h"

static GLogFunc old_handler = nullptr;

static void qt_log_handler(
    const gchar* domain,
    GLogLevelFlags level,
    const gchar* message,
    gpointer user_data)
{
    if (old_handler)
        old_handler(domain, level, message, nullptr);

    if (!(level & (G_LOG_LEVEL_ERROR |
        G_LOG_LEVEL_CRITICAL 
        // | G_LOG_LEVEL_WARNING
        )))
        return;

    QString text = QString::fromUtf8(message);

    QMetaObject::invokeMethod(
        qApp,
        [text]()
        {
            QMessageBox::critical(
                qApp->activeWindow(),
                "Error",
                text);
        },
        Qt::QueuedConnection);
}

int main(int argc, char *argv[])
{
    gst_init( &argc, &argv );

    old_handler = g_log_set_default_handler(
        qt_log_handler,
        nullptr);

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
