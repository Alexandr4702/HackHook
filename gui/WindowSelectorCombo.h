#pragma once
#include <QComboBox>

class WindowSelectorCombo : public QComboBox
{
    Q_OBJECT
  public:
    using QComboBox::QComboBox;

  signals:
    void aboutToShowPopup();

  protected:
    void showPopup() override
    {
        emit aboutToShowPopup();
        QComboBox::showPopup();
    }
};
