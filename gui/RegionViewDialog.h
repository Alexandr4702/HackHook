#pragma once

#include "myhook/FoundOccurrences.h"
#include <QDialog>
#include <QString>
#include <cstdint>
#include <vector>

namespace Ui
{
class RegionViewDialog;
}

class QAbstractItemModel;

struct MemoryRegionDetails
{
    bool available = false;
    uint64_t address = 0;
    uint64_t baseAddress = 0;
    uint64_t allocationBase = 0;
    uint32_t allocationProtect = 0;
    uint64_t regionSize = 0;
    uint32_t state = 0;
    uint32_t protect = 0;
    uint32_t type = 0;
    uint64_t moduleBase = 0;
    uint64_t moduleSize = 0;
    QString moduleName;
    QString modulePath;
    QString mappedPath;
    bool isHeap = false;
    uint64_t heapHandle = 0;
    uint64_t heapBlock = 0;
    uint64_t heapBlockSize = 0;
    uint32_t heapFlags = 0;
    bool workingSetQueried = false;
    bool workingSetValid = false;
    uint32_t workingSetProtect = 0;
    uint32_t numaNode = 0;
    uint32_t shareCount = 0;
    bool shared = false;
    bool locked = false;
    bool largePage = false;
    bool bad = false;
};

class RegionViewDialog : public QDialog
{
    Q_OBJECT

  public:
    RegionViewDialog(FoundOccurrences selectedOccurrence, std::vector<FoundOccurrences> regionOccurrences,
                     std::vector<uint8_t> data, MemoryRegionDetails details, QString windowName,
                     QWidget *parent = nullptr);
    ~RegionViewDialog() override;
    void setRegionData(std::vector<uint8_t> data, MemoryRegionDetails details);
    void setUpdating(bool updating);

  signals:
    void updateRequested();

  private:
    Ui::RegionViewDialog *ui;
    QAbstractItemModel *m_model = nullptr;
    FoundOccurrences m_selectedOccurrence;
};
