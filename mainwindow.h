#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QSlider;

class MainToolBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onRingingCall();
    void onHangUp();
    void onHelp();
    void onSettings();

private:
    Ui::MainWindow *ui;

    MainToolBar* m_mainToolbar;
    QSlider* m_volume;
};
#endif // MAINWINDOW_H
