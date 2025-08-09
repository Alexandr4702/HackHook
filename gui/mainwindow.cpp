#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <windows.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_sender(BUFFER_NAME_TX, true)
{
    ui->setupUi(this);
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this, &MainWindow::onWindowSelectorOpened);
}

MainWindow::~MainWindow()
{
    delete ui;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd)) return TRUE;

    int length = GetWindowTextLengthW(hwnd);
    if (length == 0) return TRUE;

    std::wstring title(length, L'\0');
    GetWindowTextW(hwnd, &title[0], length + 1);

    auto comboBox = reinterpret_cast<QComboBox*>(lParam);
    comboBox->addItem(QString::fromStdWString(title));
    return TRUE;
}

void MainWindow::refreshWindowList()
{
    ui->windowSelectorCombo->clear();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(ui->windowSelectorCombo));
}

void MainWindow::onWindowSelectorOpened()
{
    ui->windowSelectorCombo->clear();

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;

        int length = GetWindowTextLengthW(hwnd);
        if (length == 0) return TRUE;

        std::wstring title(length, L'\0');
        GetWindowTextW(hwnd, &title[0], length + 1);

        auto combo = reinterpret_cast<QComboBox*>(lParam);
        combo->addItem(QString::fromStdWString(title));
        return TRUE;
    }, reinterpret_cast<LPARAM>(ui->windowSelectorCombo));
}


void MainWindow::on_hookButton_clicked()
{
    if (!m_injector.isHooked()) {
        std::wstring windowName = ui->windowSelectorCombo->currentText().toStdWString();
        qDebug() << "hookInjected " << windowName;
        if(windowName.empty())
            return;
        if(m_injector.hook(windowName))
            ui->hookButton->setText("Unhook");
    } else {
        m_injector.unhook();
        qDebug() << "unooked";
        ui->hookButton->setText("Inject Hook");
    }
}