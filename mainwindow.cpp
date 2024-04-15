#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "maintoolbar.h"
#include "preferences.h"

#include "sendrecv.h"
#include "version.h"

#include <QMessageBox>
#include <QSlider>

#define STRINGIZE_(str) #str
#define STRINGIZE(x) STRINGIZE_(x)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_mainToolbar(new MainToolBar(this))
{
    setWindowIcon(QIcon(":/ico.png"));

    ui->setupUi(this);
    ui->toolBar->addWidget(m_mainToolbar);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setMaximumWidth(100);
    m_volume->setRange(0, 100);
    m_volume->setValue(100);
    statusBar()->addPermanentWidget(m_volume);


    // Set the text edit widget to read-only mode
    ui->chatDisplay->setReadOnly(true);
    // Set the text edit widget to word wrap mode
    ui->chatDisplay->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    // Set the line edit widget to clear its text when return is pressed
    ui->chatInput->setClearButtonEnabled(true);

    // Set the push button widget to be enabled only when the line edit widget has some text
    ui->sendButton->setEnabled(false);


    connect(m_mainToolbar, &MainToolBar::ringingCall, this, &MainWindow::onRingingCall);
    connect(m_mainToolbar, &MainToolBar::hangUp, this, &MainWindow::onHangUp);
    connect(m_mainToolbar, &MainToolBar::help, this, &MainWindow::onHelp);
    connect(m_mainToolbar, &MainToolBar::settings, this, &MainWindow::onSettings);


    connect(ui->chatInput, &QLineEdit::textChanged, this, &MainWindow::onChatInputTextChanged);
    connect(ui->chatInput, &QLineEdit::returnPressed, this, &MainWindow::onChatInputReturnPressed);
    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::onSendButtonClicked);

    connect(this, &MainWindow::messageReceived, this, &MainWindow::onMessageReceived);
}

MainWindow::~MainWindow()
{
    cleanup_and_quit_loop("hand up", false);
    delete ui;
}

void MainWindow::onRingingCall()
{
    start_sendrecv(ui->videoArea->winId(), this);
}

void MainWindow::onHangUp()
{
    cleanup_and_quit_loop("hand up", false);
}

void MainWindow::onHelp()
{
    raise();
    activateWindow();
    QMessageBox::about(
        this,
        tr("About webrtc-ui"),
        "\nVersion " STRINGIZE(GIT_COMMIT) "\n" + 
        QApplication::organizationDomain() + '/' + QApplication::organizationName() + '/' + QApplication::applicationName());

}

void MainWindow::onSettings()
{
    raise();
    activateWindow();

    Preferences prefDlg(this);
    prefDlg.exec();
}

// A slot that is called when the text of the chat input changes
void MainWindow::onChatInputTextChanged(const QString& text)
{
    // Enable or disable the send button depending on whether the text is empty or not
    ui->sendButton->setEnabled(!text.isEmpty());
}

// A slot that is called when the return key is pressed on the chat input
void MainWindow::onChatInputReturnPressed()
{
    // Send the text of the chat input as a message
    sendMessage(ui->chatInput->text());
}

// A slot that is called when the send button is clicked
void MainWindow::onSendButtonClicked()
{
    // Send the text of the chat input as a message
    sendMessage(ui->chatInput->text());
}

// A slot that is called when a message is received from another user
void MainWindow::onMessageReceived(const QString& message)
{
    // Display the message on the chat display with the sender's name and a different text color
    ui->chatDisplay->append(QStringLiteral("<font color=blue>%1</font>").arg(message));// , Qt::blue);
}

// A helper function that sends a message to another user
void MainWindow::sendMessage(const QString& message)
{
    // Display the message on the chat display with the sender's name and a different text color
    ui->chatDisplay->append(QStringLiteral("<font color=red>%1</font>").arg(message));// , Qt::red);

    // Emit a signal that the message has been sent
    emit messageSent(message);

    // Clear the chat input
    ui->chatInput->clear();
}

void MainWindow::handleRecv(uintptr_t id, const char* data)
{
    if (!data || !(*data))
        return;

    if (id != m_channelId)
    {
        if (m_channelId == 0)
            m_channelId = id;
        else
            return;
    }

    emit messageReceived(data);
}

QMetaObject::Connection MainWindow::setAudioVolumeLambda(std::function<void(int)> lambda)
{
    return connect(m_volume, &QSlider::valueChanged, lambda);
}

QMetaObject::Connection MainWindow::setSendLambda(std::function<void(const QString&)> lambda)
{
    return connect(this, &MainWindow::messageSent, lambda);
}

void MainWindow::onQuit()
{
    disconnect(this, &MainWindow::messageSent, nullptr, nullptr);
    m_channelId = 0;
    // TODO: hang up
}
