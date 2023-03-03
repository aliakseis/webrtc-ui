#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "maintoolbar.h"
#include "preferences.h"

#include "sendrecv.h"

#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_mainToolbar(new MainToolBar(this))
{
    setWindowIcon(QIcon(":/ico.png"));

    ui->setupUi(this);
    ui->toolBar->addWidget(m_mainToolbar);

    connect(m_mainToolbar, &MainToolBar::ringingCall, this, &MainWindow::onRingingCall);
    connect(m_mainToolbar, &MainToolBar::hangUp, this, &MainWindow::onHangUp);
    connect(m_mainToolbar, &MainToolBar::help, this, &MainWindow::onHelp);
    connect(m_mainToolbar, &MainToolBar::settings, this, &MainWindow::onSettings);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onRingingCall()
{
    start_sendrecv(ui->centralwidget->winId());
}

void MainWindow::onHangUp()
{
    cleanup_and_quit_loop("hand up", HANG_UP);
}

void MainWindow::onHelp()
{
    raise();
    activateWindow();
    QMessageBox::about(
        this,
        tr("About webrtc-ui"),
        '\n' + QApplication::organizationDomain() + '/' + QApplication::organizationName() + '/' + QApplication::applicationName());

}

void MainWindow::onSettings()
{
    raise();
    activateWindow();

    Preferences prefDlg(this);
    prefDlg.exec();
}
