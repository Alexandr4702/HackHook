#include "RegionViewDialog.h"
#include "OccurrenceColors.h"
#include "common/utility.h"
#include "interface_generated.h"
#include "ui_RegionViewDialog.h"
#include <QAbstractTableModel>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QMessageBox>
#include <QSaveFile>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <algorithm>

MemoryRegionDetails MemoryRegionDetails::fromFlatbuffer(const Interface::MemoryRegionInfo *region)
{
    MemoryRegionDetails details;
    if (!region)
        return details;

    const auto stringFromBuffer = [](const flatbuffers::String *value) {
        return value ? QString::fromUtf8(value->c_str(), static_cast<qsizetype>(value->size())) : QString{};
    };

    details.available = true;
    details.address = region->address();
    details.baseAddress = region->base_address();
    details.allocationBase = region->allocation_base();
    details.allocationProtect = region->allocation_protect();
    details.regionSize = region->region_size();
    details.state = region->state();
    details.protect = region->protect();
    details.type = region->type();
    details.moduleBase = region->module_base();
    details.moduleSize = region->module_size();
    details.moduleName = stringFromBuffer(region->module_name());
    details.modulePath = stringFromBuffer(region->module_path());
    details.mappedPath = stringFromBuffer(region->mapped_path());
    details.isHeap = region->is_heap();
    details.heapHandle = region->heap_handle();
    details.heapBlock = region->heap_block();
    details.heapBlockSize = region->heap_block_size();
    details.heapFlags = region->heap_flags();
    details.workingSetQueried = region->working_set_queried();
    details.workingSetValid = region->working_set_valid();
    details.workingSetProtect = region->working_set_protect();
    details.numaNode = region->numa_node();
    details.shareCount = region->share_count();
    details.shared = region->shared();
    details.locked = region->locked();
    details.largePage = region->large_page();
    details.bad = region->bad();
    return details;
}

namespace
{
constexpr qsizetype bytesPerRow = 16;

QString safeFileName(QString name)
{
    static const QString invalidCharacters = QStringLiteral("<>:\"/\\|?*");
    for (qsizetype i = 0; i < name.size(); ++i)
    {
        if (name[i].unicode() < 0x20 || invalidCharacters.contains(name[i]))
            name[i] = QLatin1Char('_');
    }

    while (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
        name.chop(1);

    return name.isEmpty() ? QStringLiteral("window") : name;
}

QString hexValue(uint64_t value)
{
    return QString("0x%1").arg(value, 0, 16);
}

QString memoryState(uint32_t state)
{
    switch (state)
    {
    case 0x1000:
        return QStringLiteral("Commit");
    case 0x2000:
        return QStringLiteral("Reserve");
    case 0x10000:
        return QStringLiteral("Free");
    default:
        return QStringLiteral("Unknown");
    }
}

QString memoryType(uint32_t type)
{
    switch (type)
    {
    case 0x20000:
        return QStringLiteral("Private");
    case 0x40000:
        return QStringLiteral("Mapped");
    case 0x1000000:
        return QStringLiteral("Image");
    default:
        return QStringLiteral("Unknown");
    }
}

QString memoryProtection(uint32_t protection)
{
    QStringList flags;
    switch (protection & 0xff)
    {
    case 0x01:
        flags << QStringLiteral("No access");
        break;
    case 0x02:
        flags << QStringLiteral("Read");
        break;
    case 0x04:
        flags << QStringLiteral("Read/Write");
        break;
    case 0x08:
        flags << QStringLiteral("Copy-on-write");
        break;
    case 0x10:
        flags << QStringLiteral("Execute");
        break;
    case 0x20:
        flags << QStringLiteral("Execute/Read");
        break;
    case 0x40:
        flags << QStringLiteral("Execute/Read/Write");
        break;
    case 0x80:
        flags << QStringLiteral("Execute/Copy-on-write");
        break;
    default:
        flags << QStringLiteral("Unknown");
        break;
    }
    if (protection & 0x100)
        flags << QStringLiteral("Guard");
    if (protection & 0x200)
        flags << QStringLiteral("No cache");
    if (protection & 0x400)
        flags << QStringLiteral("Write combine");
    if (protection & 0x40000000)
        flags << QStringLiteral("Targets invalid");
    return flags.join(QStringLiteral(" | "));
}

QString heapFlags(uint32_t flags)
{
    QStringList values;
    if (flags & 0x1)
        values << QStringLiteral("Region");
    if (flags & 0x2)
        values << QStringLiteral("Uncommitted range");
    if (flags & 0x4)
        values << QStringLiteral("Busy");
    if (flags & 0x10)
        values << QStringLiteral("Moveable");
    if (flags & 0x20)
        values << QStringLiteral("DDE shared");
    return values.isEmpty() ? QStringLiteral("None") : values.join(QStringLiteral(" | "));
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString occurrenceValue(const FoundOccurrences &occurrence, const std::vector<uint8_t> &regionData)
{
    if (occurrence.offset >= regionData.size() || occurrence.data_size > regionData.size() - occurrence.offset)
        return QStringLiteral("<unavailable>");

    const auto *begin = regionData.data() + static_cast<size_t>(occurrence.offset);
    const auto bytes = std::span<const uint8_t>(begin, static_cast<size_t>(occurrence.data_size));
    const std::string decoded = valueToString(bytes, static_cast<Interface::ValueType>(occurrence.type));
    QString value = QString::fromUtf8(decoded.data(), static_cast<qsizetype>(decoded.size()));
    for (qsizetype i = 0; i < value.size(); ++i)
    {
        if (!value[i].isPrint())
            value[i] = QLatin1Char('.');
    }

    constexpr qsizetype maxLength = 80;
    if (value.size() > maxLength)
        value = value.left(maxLength - 3) + QStringLiteral("...");
    return value.isEmpty() ? QStringLiteral("<empty>") : value;
}

class MatchComboDelegate final : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

  protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QColor color = index.data(Qt::BackgroundRole).value<QColor>();
        if (!color.isValid())
            return;

        option->backgroundBrush = color;
        option->palette.setColor(QPalette::Base, color);
        option->palette.setColor(QPalette::Highlight, color);
        option->palette.setColor(QPalette::Text, Qt::black);
        option->palette.setColor(QPalette::HighlightedText, Qt::black);
    }
};

void populateRegionProperties(QTreeWidget *properties, const MemoryRegionDetails &details)
{
    properties->clear();
    auto addProperty = [properties](const QString &name, const QString &value) {
        auto *item = new QTreeWidgetItem(properties, {name, value});
        item->setToolTip(1, value);
    };

    if (!details.available)
    {
        addProperty(RegionViewDialog::tr("Metadata"), RegionViewDialog::tr("Unavailable"));
        return;
    }

    addProperty(RegionViewDialog::tr("Query address"), hexValue(details.address));
    addProperty(RegionViewDialog::tr("Region base"), hexValue(details.baseAddress));
    addProperty(RegionViewDialog::tr("Region end"), hexValue(details.baseAddress + details.regionSize));
    addProperty(RegionViewDialog::tr("Region size"),
                QString("%1 (%2 bytes)").arg(hexValue(details.regionSize)).arg(details.regionSize));
    addProperty(RegionViewDialog::tr("Allocation base"), hexValue(details.allocationBase));
    addProperty(RegionViewDialog::tr("State"),
                QString("%1 (%2)").arg(memoryState(details.state), hexValue(details.state)));
    addProperty(RegionViewDialog::tr("Type"),
                QString("%1 (%2)").arg(memoryType(details.type), hexValue(details.type)));
    addProperty(RegionViewDialog::tr("Protection"),
                QString("%1 (%2)").arg(memoryProtection(details.protect), hexValue(details.protect)));
    addProperty(RegionViewDialog::tr("Allocation protection"),
                QString("%1 (%2)")
                    .arg(memoryProtection(details.allocationProtect), hexValue(details.allocationProtect)));
    addProperty(RegionViewDialog::tr("Module"), details.moduleName.isEmpty() ? QStringLiteral("-") : details.moduleName);
    addProperty(RegionViewDialog::tr("Module base"),
                details.moduleBase == 0 ? QStringLiteral("-") : hexValue(details.moduleBase));
    addProperty(RegionViewDialog::tr("Module size"),
                details.moduleSize == 0 ? QStringLiteral("-") : QString::number(details.moduleSize));
    addProperty(RegionViewDialog::tr("Module path"),
                details.modulePath.isEmpty() ? QStringLiteral("-") : details.modulePath);
    addProperty(RegionViewDialog::tr("Mapped path"),
                details.mappedPath.isEmpty() ? QStringLiteral("-") : details.mappedPath);
    addProperty(RegionViewDialog::tr("Heap block (best effort)"), yesNo(details.isHeap));
    if (details.isHeap)
    {
        addProperty(RegionViewDialog::tr("Heap handle"), hexValue(details.heapHandle));
        addProperty(RegionViewDialog::tr("Heap block"), hexValue(details.heapBlock));
        addProperty(RegionViewDialog::tr("Heap block size"), QString::number(details.heapBlockSize));
        addProperty(RegionViewDialog::tr("Heap flags"),
                    QString("%1 (%2)").arg(heapFlags(details.heapFlags), hexValue(details.heapFlags)));
    }
    addProperty(RegionViewDialog::tr("Working set query"),
                details.workingSetQueried ? RegionViewDialog::tr("Available")
                                          : RegionViewDialog::tr("Unavailable"));
    if (details.workingSetQueried)
    {
        addProperty(RegionViewDialog::tr("Resident"), yesNo(details.workingSetValid));
        addProperty(RegionViewDialog::tr("Shared"), yesNo(details.shared));
        addProperty(RegionViewDialog::tr("Bad page"), yesNo(details.bad));
        if (details.workingSetValid)
        {
            addProperty(RegionViewDialog::tr("Working set protection"),
                        QString("%1 (%2)")
                            .arg(memoryProtection(details.workingSetProtect), hexValue(details.workingSetProtect)));
            addProperty(RegionViewDialog::tr("NUMA node"), QString::number(details.numaNode));
            addProperty(RegionViewDialog::tr("Share count"), QString::number(details.shareCount));
            addProperty(RegionViewDialog::tr("Locked"), yesNo(details.locked));
            addProperty(RegionViewDialog::tr("Large page"), yesNo(details.largePage));
        }
    }
}

class RegionHexModel final : public QAbstractTableModel
{
  public:
    RegionHexModel(std::vector<FoundOccurrences> occurrences, std::vector<uint8_t> data, QObject *parent)
        : QAbstractTableModel(parent), m_occurrences(std::move(occurrences)), m_data(std::move(data))
    {
        for (const auto &occurrence : m_occurrences)
            m_maxPatternSize = std::max(m_maxPatternSize, occurrence.data_size);
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        if (parent.isValid())
            return 0;
        return static_cast<int>((m_data.size() + bytesPerRow - 1) / bytesPerRow);
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(bytesPerRow + 2);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};

        const size_t begin = static_cast<size_t>(index.row()) * bytesPerRow;
        const size_t end = std::min(begin + bytesPerRow, m_data.size());
        const bool byteColumn = index.column() >= 1 && index.column() <= bytesPerRow;
        const size_t byteOffset = begin + static_cast<size_t>(index.column() - 1);
        size_t matchIndex = m_occurrences.size();
        if (byteColumn && byteOffset < m_data.size() && m_maxPatternSize != 0)
        {
            auto upper = std::upper_bound(
                m_occurrences.begin(), m_occurrences.end(), byteOffset,
                [](size_t offset, const FoundOccurrences &occurrence) { return offset < occurrence.offset; });
            size_t i = static_cast<size_t>(std::distance(m_occurrences.begin(), upper));
            while (i != 0)
            {
                --i;
                const auto &occurrence = m_occurrences[i];
                const uint64_t distance = byteOffset - occurrence.offset;
                if (distance >= m_maxPatternSize)
                    break;
                if (distance < occurrence.data_size)
                {
                    matchIndex = i;
                    break;
                }
            }
        }
        const bool matchedByte = matchIndex != m_occurrences.size();

        if (matchedByte && role == Qt::BackgroundRole)
            return occurrenceColor(matchIndex);
        if (matchedByte && role == Qt::ForegroundRole)
            return QColor(Qt::black);
        if (matchedByte && role == Qt::ToolTipRole)
            return QString("Found pattern #%1 at offset %2")
                .arg(matchIndex + 1)
                .arg(m_occurrences[matchIndex].offset);
        if (role != Qt::DisplayRole)
            return {};

        if (index.column() == 0)
            return QString("0x%1").arg(m_occurrences.front().baseAddress + begin, 16, 16, QLatin1Char('0'));

        if (byteColumn)
            return byteOffset < m_data.size()
                       ? QVariant(QString("%1").arg(m_data[byteOffset], 2, 16, QLatin1Char('0')).toUpper())
                       : QVariant{};

        QString text;
        text.reserve(static_cast<qsizetype>(end - begin));
        for (size_t i = begin; i < end; ++i)
        {
            const uint8_t byte = m_data[i];
            text += byte >= 0x20 && byte <= 0x7e ? QChar(byte) : QLatin1Char('.');
        }
        return text;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};

        if (section == 0)
            return QStringLiteral("Address");
        if (section == bytesPerRow + 1)
            return QStringLiteral("ASCII");
        if (section >= 1 && section <= bytesPerRow)
            return QString("%1").arg(section - 1, 2, 16, QLatin1Char('0')).toUpper();
        return {};
    }

    const std::vector<uint8_t> &bytes() const noexcept
    {
        return m_data;
    }

    const std::vector<FoundOccurrences> &occurrences() const noexcept
    {
        return m_occurrences;
    }

    void replaceData(std::vector<uint8_t> data)
    {
        beginResetModel();
        m_data = std::move(data);
        endResetModel();
    }

    QModelIndex occurrenceIndex(size_t occurrenceIndex) const
    {
        if (occurrenceIndex >= m_occurrences.size())
            return {};

        const uint64_t offset = m_occurrences[occurrenceIndex].offset;
        if (offset >= m_data.size())
            return {};

        const int row = static_cast<int>(offset / bytesPerRow);
        const int column = static_cast<int>(offset % bytesPerRow) + 1;
        return index(row, column);
    }

  private:
    std::vector<FoundOccurrences> m_occurrences;
    std::vector<uint8_t> m_data;
    uint64_t m_maxPatternSize = 0;
};
} // namespace

RegionViewDialog::RegionViewDialog(FoundOccurrences selectedOccurrence,
                                   std::vector<FoundOccurrences> regionOccurrences, std::vector<uint8_t> data,
                                   MemoryRegionDetails details, QString windowName, QWidget *parent)
    : QDialog(parent), ui(new Ui::RegionViewDialog), m_selectedOccurrence(selectedOccurrence)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("Region 0x%1").arg(selectedOccurrence.baseAddress, 0, 16));

    ui->summaryLabel->setText(
        QString("Base: 0x%1    Region size: %2    Read: %3    Matches: %4    Selected offset: %5")
            .arg(selectedOccurrence.baseAddress, 0, 16)
            .arg(selectedOccurrence.region_size)
            .arg(data.size())
            .arg(regionOccurrences.size())
            .arg(selectedOccurrence.offset));

    auto *properties = ui->propertiesTree;
    populateRegionProperties(properties, details);
    properties->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    properties->header()->setSectionResizeMode(1, QHeaderView::Stretch);

    size_t selectedIndex = 0;
    for (size_t i = 0; i < regionOccurrences.size(); ++i)
    {
        const auto &occurrence = regionOccurrences[i];
        if (occurrence.baseAddress == selectedOccurrence.baseAddress && occurrence.offset == selectedOccurrence.offset &&
            occurrence.region_size == selectedOccurrence.region_size && occurrence.data_size == selectedOccurrence.data_size &&
            occurrence.type == selectedOccurrence.type)
        {
            selectedIndex = i;
            break;
        }
    }

    auto *previousButton = ui->previousButton;
    auto *matchCombo = ui->matchCombo;
    auto *nextButton = ui->nextButton;
    matchCombo->setItemDelegate(new MatchComboDelegate(matchCombo));
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    matchCombo->setLabelDrawingMode(QComboBox::LabelDrawingMode::UseDelegate);
#endif
    for (size_t i = 0; i < regionOccurrences.size(); ++i)
    {
        const auto &occurrence = regionOccurrences[i];
        matchCombo->addItem(QString("Match %1 | %2 | 0x%3 (+%4), %5 bytes")
                                .arg(i + 1)
                                .arg(occurrenceValue(occurrence, data))
                                .arg(occurrence.baseAddress + occurrence.offset, 0, 16)
                                .arg(occurrence.offset)
                                .arg(occurrence.data_size));
        matchCombo->setItemData(static_cast<int>(i), occurrenceColor(i), Qt::BackgroundRole);
        matchCombo->setItemData(static_cast<int>(i), QColor(Qt::black), Qt::ForegroundRole);
    }
    previousButton->setEnabled(regionOccurrences.size() > 1);
    nextButton->setEnabled(regionOccurrences.size() > 1);

    auto *table = ui->regionTable;
    auto *model = new RegionHexModel(std::move(regionOccurrences), std::move(data), table);
    m_model = model;
    table->setModel(model);
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column <= bytesPerRow; ++column)
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(bytesPerRow + 1, QHeaderView::Stretch);

    auto navigateTo = [table, model](int index) {
        const QModelIndex match = model->occurrenceIndex(static_cast<size_t>(index));
        if (match.isValid())
            table->scrollTo(match, QAbstractItemView::PositionAtCenter);
    };
    connect(matchCombo, &QComboBox::currentIndexChanged, this, navigateTo);
    connect(previousButton, &QPushButton::clicked, this, [matchCombo]() {
        const int count = matchCombo->count();
        if (count != 0)
            matchCombo->setCurrentIndex((matchCombo->currentIndex() + count - 1) % count);
    });
    connect(nextButton, &QPushButton::clicked, this, [matchCombo]() {
        const int count = matchCombo->count();
        if (count != 0)
            matchCombo->setCurrentIndex((matchCombo->currentIndex() + 1) % count);
    });

    auto *nextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    auto *previousShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(nextShortcut, &QShortcut::activated, nextButton, &QPushButton::click);
    connect(previousShortcut, &QShortcut::activated, previousButton, &QPushButton::click);
    connect(ui->updateButton, &QPushButton::clicked, this, &RegionViewDialog::updateRequested);

    auto *saveButton = ui->saveButton;
    connect(saveButton, &QPushButton::clicked, this,
            [this, model, selectedOccurrence, windowName = safeFileName(std::move(windowName))]() {
        const QString suggestedName =
            QString("%1_0x%2.bin").arg(windowName).arg(selectedOccurrence.baseAddress, 0, 16);
        const QString path = QFileDialog::getSaveFileName(this, tr("Save region binary"), suggestedName,
                                                          tr("Binary files (*.bin);;All files (*)"));
        if (path.isEmpty())
            return;

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
        {
            QMessageBox::warning(this, tr("Save binary"), tr("Cannot open the selected file for writing."));
            return;
        }

        const auto &bytes = model->bytes();
        const auto written = bytes.empty()
                                 ? qint64{0}
                                 : file.write(reinterpret_cast<const char *>(bytes.data()),
                                              static_cast<qint64>(bytes.size()));
        if (written != static_cast<qint64>(bytes.size()) || !file.commit())
            QMessageBox::warning(this, tr("Save binary"), tr("Failed to save the complete region."));
    });
    matchCombo->setCurrentIndex(static_cast<int>(selectedIndex));
    navigateTo(static_cast<int>(selectedIndex));
}

RegionViewDialog::~RegionViewDialog()
{
    delete ui;
}

void RegionViewDialog::setRegionData(std::vector<uint8_t> data, MemoryRegionDetails details)
{
    auto *model = static_cast<RegionHexModel *>(m_model);
    model->replaceData(std::move(data));
    populateRegionProperties(ui->propertiesTree, details);

    const auto &occurrences = model->occurrences();
    const auto &bytes = model->bytes();
    for (size_t i = 0; i < occurrences.size(); ++i)
    {
        const auto &occurrence = occurrences[i];
        ui->matchCombo->setItemText(
            static_cast<int>(i), QString("Match %1 | %2 | 0x%3 (+%4), %5 bytes")
                                     .arg(i + 1)
                                     .arg(occurrenceValue(occurrence, bytes))
                                     .arg(occurrence.baseAddress + occurrence.offset, 0, 16)
                                     .arg(occurrence.offset)
                                     .arg(occurrence.data_size));
    }

    ui->summaryLabel->setText(
        QString("Base: 0x%1    Region size: %2    Read: %3    Matches: %4    Selected offset: %5")
            .arg(m_selectedOccurrence.baseAddress, 0, 16)
            .arg(m_selectedOccurrence.region_size)
            .arg(bytes.size())
            .arg(occurrences.size())
            .arg(m_selectedOccurrence.offset));
}

void RegionViewDialog::setUpdating(bool updating)
{
    ui->updateButton->setEnabled(!updating);
    ui->updateButton->setText(updating ? tr("Updating...") : tr("Update"));
}
