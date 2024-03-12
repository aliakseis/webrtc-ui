#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "isendrecv.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QSlider;

class MainToolBar;

class MainWindow : public QMainWindow, public ISendRecv
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    // A signal that is emitted when a message is sent to another user
    void messageSent(const QString& message);
    void messageReceived(const QString& message);

private slots:
    void onRingingCall();
    void onHangUp();
    void onHelp();
    void onSettings();

    // A slot that is called when the text of the chat input changes
    void onChatInputTextChanged(const QString& text);

    // A slot that is called when the return key is pressed on the chat input
    void onChatInputReturnPressed();

    // A slot that is called when the send button is clicked
    void onSendButtonClicked();

    // A slot that is called when a message is received from another user
    void onMessageReceived(const QString& message);
 
private:
    // A helper function that sends a message to another user
    void sendMessage(const QString& message);

private:
    Ui::MainWindow *ui;

    MainToolBar* m_mainToolbar;
    QSlider* m_volume;

    uintptr_t m_channelId = 0;

    // Inherited via ISendRecv
    void handleRecv(uintptr_t id, const char* data) override;
    void setSendLambda(std::function<void(const QString&)> lambda) override;
    void onQuit() override;
};
#endif // MAINWINDOW_H
