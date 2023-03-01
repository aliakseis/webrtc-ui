#include "maintoolbar.h"

#include <QStyle>

#include "ui_maintoolbar.h"

const char PROP_ACTIVE[] = "active";

MainToolBar::MainToolBar(QWidget* parent)
    : QWidget(parent), m_activeTabIndex(HANG_UP_TAB), ui(new Ui::MainToolBar)
{
    ui->setupUi(this);
    m_tabs = { {RINGING_CALL_TAB, ui->btnRingingCall},
               {HANG_UP_TAB, ui->btnHangUp} };
    activateTab(m_activeTabIndex);
}

MainToolBar::~MainToolBar() { delete ui; }

void MainToolBar::on_btnRingingCall_clicked()
{
    if (m_activeTabIndex != RINGING_CALL_TAB)
    {
        activateTab(RINGING_CALL_TAB);
        emit ringingCall();
    }
}

void MainToolBar::on_btnHangUp_clicked()
{
    if (m_activeTabIndex != HANG_UP_TAB)
    {
        activateTab(HANG_UP_TAB);
        emit hangUp();
    }
}


void MainToolBar::on_btnHelp_clicked() { emit help(); }

void MainToolBar::on_btnSettings_clicked() { emit settings(); }

void MainToolBar::activateTab(MainToolBar::TabId index)
{
    Q_ASSERT(index >= 0 && index < (int)m_tabs.size());
    m_activeTabIndex = index;

    for (auto& tab : m_tabs)
    {
        tab.second->setProperty(PROP_ACTIVE, (tab.first == index));
        tab.second->style()->unpolish(tab.second);
        tab.second->style()->polish(tab.second);
    }
}
