#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "maintoolbar.h"
#include "preferences.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_mainToolbar(new MainToolBar(this))
{
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
{}

void MainWindow::onHangUp()
{}

void MainWindow::onHelp()
{}

void MainWindow::onSettings()
{
    raise();
    activateWindow();

    Preferences prefDlg(this);
    prefDlg.exec();
}
