#pragma once

#include "app/view/app_page.h"

class SettingsPage : public AppPage
{
    Q_OBJECT

  public:
    explicit SettingsPage(QWidget *parent = nullptr);

  signals:
    void terminalRequested();
};
