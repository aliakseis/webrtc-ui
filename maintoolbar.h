#pragma once

#include <QWidget>
#include <utility>
#include <vector>

namespace Ui
{
class MainToolBar;
}

class QToolButton;

class MainToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit MainToolBar(QWidget* parent = 0);
    ~MainToolBar();

Q_SIGNALS:
    void ringingCall();
    void hangUp();
    void help();
    void settings();

private Q_SLOTS:
    void on_btnRingingCall_clicked();
    void on_btnHangUp_clicked();
    void on_btnHelp_clicked();
    void on_btnSettings_clicked();

private:
    enum TabId
    {
        RINGING_CALL_TAB,
        HANG_UP_TAB,
    };

    void activateTab(MainToolBar::TabId index);
    std::vector<std::pair<TabId, QToolButton*> > m_tabs;
    TabId m_activeTabIndex;
    Ui::MainToolBar* ui;
};
