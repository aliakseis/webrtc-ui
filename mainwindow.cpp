#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "maintoolbar.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_mainToolbar(new MainToolBar(this))
{
    ui->setupUi(this);
    ui->toolBar->addWidget(m_mainToolbar);
}

MainWindow::~MainWindow()
{
    delete ui;
}

