#include "RegionViewDialog.h"
#include "OccurrenceColors.h"
#include <QAbstractTableModel>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

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
                                   QString windowName, QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("Region 0x%1").arg(selectedOccurrence.baseAddress, 0, 16));
    resize(900, 600);

    auto *layout = new QVBoxLayout(this);
    auto *details = new QLabel(
        QString("Base: 0x%1    Region size: %2    Read: %3    Matches: %4    Selected offset: %5")
            .arg(selectedOccurrence.baseAddress, 0, 16)
            .arg(selectedOccurrence.region_size)
            .arg(data.size())
            .arg(regionOccurrences.size())
            .arg(selectedOccurrence.offset),
        this);
    layout->addWidget(details);

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

    auto *navigationLayout = new QHBoxLayout;
    auto *previousButton = new QPushButton(tr("Previous"), this);
    auto *matchCombo = new QComboBox(this);
    auto *nextButton = new QPushButton(tr("Next"), this);
    for (size_t i = 0; i < regionOccurrences.size(); ++i)
    {
        const auto &occurrence = regionOccurrences[i];
        matchCombo->addItem(QString("Match %1 — 0x%2 (+%3), %4 bytes")
                                .arg(i + 1)
                                .arg(occurrence.baseAddress + occurrence.offset, 0, 16)
                                .arg(occurrence.offset)
                                .arg(occurrence.data_size));
        matchCombo->setItemData(static_cast<int>(i), occurrenceColor(i), Qt::BackgroundRole);
        matchCombo->setItemData(static_cast<int>(i), QColor(Qt::black), Qt::ForegroundRole);
    }
    matchCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    previousButton->setEnabled(regionOccurrences.size() > 1);
    nextButton->setEnabled(regionOccurrences.size() > 1);
    navigationLayout->addWidget(previousButton);
    navigationLayout->addWidget(matchCombo, 1);
    navigationLayout->addWidget(nextButton);
    layout->addLayout(navigationLayout);

    auto *table = new QTableView(this);
    auto *model = new RegionHexModel(std::move(regionOccurrences), std::move(data), table);
    table->setModel(model);
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column <= bytesPerRow; ++column)
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(bytesPerRow + 1, QHeaderView::Stretch);
    layout->addWidget(table);

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

    auto *saveButton = new QPushButton(tr("Save binary..."), this);
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
    layout->addWidget(saveButton, 0, Qt::AlignRight);

    matchCombo->setCurrentIndex(static_cast<int>(selectedIndex));
    navigateTo(static_cast<int>(selectedIndex));
}
