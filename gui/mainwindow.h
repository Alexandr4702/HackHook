#pragma once

#include <QMainWindow>
#include "WindowSelectorCombo.h"
#include "injector/Injector.h"
#include "common/utility.h"

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
    void on_hookButton_clicked();

private:
    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
};
