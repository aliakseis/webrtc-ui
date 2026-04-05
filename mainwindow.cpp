#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "maintoolbar.h"
#include "preferences.h"

#include "sendrecv.h"
#include "version.h"

#include "globals.h"

#include <QMessageBox>
#include <QSlider>
#include <QSettings>

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
    // Construct Settings from Qt QSettings (UI layer remains Qt-enabled)
    Settings settings;

    QString savePath = QSettings().value(SETTING_SAVE_PATH).toString();
#ifdef _WIN32
    // On Windows we use std::wstring for path storage
    settings.save_path = savePath.toStdWString();
#else
    settings.save_path = savePath.toStdString();
#endif

    settings.do_save = QSettings().value(SETTING_DO_SAVE).toBool();
    settings.use_turn = QSettings().value(SETTING_USE_TURN).toBool();
    settings.turn_server = QSettings().value(SETTING_TURN).toString().trimmed().toStdString();

    settings.video_launch_line =
        QSettings().value(SETTING_VIDEO_LAUNCH_LINE, VIDEO_LAUNCH_LINE_DEFAULT)
            .toString().toStdString();
    settings.audio_launch_line =
        QSettings().value(SETTING_AUDIO_LAUNCH_LINE, AUDIO_LAUNCH_LINE_DEFAULT)
            .toString().toStdString();

    // slice duration: prefer existing setting key or fallback to 0
    settings.slice_duration_secs = getSliceDurationSecs();

    settings.session_id = QSettings().value(SETTING_SESSION_ID).toString().trimmed().toStdString();

    // Window handle for rendering: use main window handle here.
    // If you used a dedicated video widget previously, use that widget's winId() instead.
    unsigned long long winid = static_cast<unsigned long long>(ui->videoArea->winId());

    // Start the send/recv subsystem with the settings copy.
    // Note: start_sendrecv signature changed to accept Settings by value.
    if (!start_sendrecv(winid, this, std::move(settings))) {
        // handle failure (log / UI feedback)
        qWarning("start_sendrecv failed");
        return;
    }

    // UI updates for ringing -> in-call state can continue here...
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


void MainWindow::onQuit()
{
    disconnect(this, &MainWindow::messageSent, nullptr, nullptr);
    m_channelId = 0;
    // TODO: hang up
}

// Disable warning about lambda capture of 'this' in a destructor, which is safe here because the lambda is only used to disconnect signals, and the destructor will disconnect all signals anyway.
#pragma warning(disable: 4573)

std::function<void()> MainWindow::setAudioVolumeLambda(std::function<void(int)> lambda)
{
    auto c = connect(m_volume, &QSlider::valueChanged, lambda);
    return [c] {
        QObject::disconnect(c);
    };
}

std::function<void()> MainWindow::setSendLambda(std::function<void(const std::string&)> lambda)
{
    // Bridge: convert QString -> std::string and forward to the provided handler.
    auto bridge = [lambda = std::move(lambda)](const QString& s) {
        lambda(s.toStdString());
    };

    auto c = connect(this, &MainWindow::messageSent, bridge);
    return [c] {
        QObject::disconnect(c);
    };
}
