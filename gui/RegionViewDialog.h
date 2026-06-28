#pragma once

#include "myhook/FoundOccurrences.h"
#include <QDialog>
#include <QString>
#include <cstdint>
#include <vector>

class RegionViewDialog : public QDialog
{
  public:
    RegionViewDialog(FoundOccurrences selectedOccurrence, std::vector<FoundOccurrences> regionOccurrences,
                     std::vector<uint8_t> data, QString windowName, QWidget *parent = nullptr);
};
