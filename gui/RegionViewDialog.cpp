#include "RegionViewDialog.h"
#include <QAbstractTableModel>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
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
    RegionHexModel(FoundOccurrences occurrence, std::vector<uint8_t> data, QObject *parent)
        : QAbstractTableModel(parent), m_occurrence(occurrence), m_data(std::move(data))
    {
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
        const bool matchedByte = byteColumn && byteOffset < m_data.size() && byteOffset >= m_occurrence.offset &&
                                 byteOffset - m_occurrence.offset < m_occurrence.data_size;

        if (matchedByte && role == Qt::BackgroundRole)
            return QColor(255, 205, 80);
        if (matchedByte && role == Qt::ForegroundRole)
            return QColor(Qt::black);
        if (matchedByte && role == Qt::ToolTipRole)
            return QStringLiteral("Found pattern");
        if (role != Qt::DisplayRole)
            return {};

        if (index.column() == 0)
            return QString("0x%1").arg(m_occurrence.baseAddress + begin, 16, 16, QLatin1Char('0'));

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

  private:
    FoundOccurrences m_occurrence;
    std::vector<uint8_t> m_data;
};
} // namespace

RegionViewDialog::RegionViewDialog(FoundOccurrences occurrence, std::vector<uint8_t> data, QString windowName,
                                   QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("Region 0x%1").arg(occurrence.baseAddress, 0, 16));
    resize(900, 600);

    auto *layout = new QVBoxLayout(this);
    auto *details = new QLabel(
        QString("Base: 0x%1    Region size: %2    Read: %3    Match offset: %4    Data size: %5")
            .arg(occurrence.baseAddress, 0, 16)
            .arg(occurrence.region_size)
            .arg(data.size())
            .arg(occurrence.offset)
            .arg(occurrence.data_size),
        this);
    layout->addWidget(details);

    auto *table = new QTableView(this);
    auto *model = new RegionHexModel(occurrence, std::move(data), table);
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

    auto *saveButton = new QPushButton(tr("Save binary..."), this);
    connect(saveButton, &QPushButton::clicked, this,
            [this, model, occurrence, windowName = safeFileName(std::move(windowName))]() {
        const QString suggestedName = QString("%1_0x%2.bin").arg(windowName).arg(occurrence.baseAddress, 0, 16);
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

    const int matchRow = static_cast<int>(occurrence.offset / bytesPerRow);
    if (matchRow < model->rowCount())
    {
        const int matchColumn = static_cast<int>(occurrence.offset % bytesPerRow) + 1;
        const QModelIndex match = model->index(matchRow, matchColumn);
        table->scrollTo(match, QAbstractItemView::PositionAtCenter);
    }
}
