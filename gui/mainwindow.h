#pragma once

#include <QMainWindow>
#include "WindowSelectorCombo.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void refreshWindowList();

    
    private slots:
    void onWindowSelectorOpened();

private:
    Ui::MainWindow *ui;
};
