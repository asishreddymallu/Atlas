#include "instruction_type_count_model.h"
#include "ui/common/table_font.h"
#include "utils/to_index.h"
namespace Atlas
{ 
namespace instr = instruction_set;

InstructionTypeCountModel::InstructionTypeCountModel(QObject *parent,
                                                     std::function<InstructionTypeCounts()> source)
    : QAbstractTableModel(parent), m_source(std::move(source))
{}

int InstructionTypeCountModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 2; // One column for the count
}

int InstructionTypeCountModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(instr::InstructionType::INSTRUCTION_TYPE_COUNT);
}

QVariant InstructionTypeCountModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal && section == 1)
    {
        return QString("Instructions retired so far, by type: the per-line counts in the "
                       "editor's margin, summed. A loop's lines count once per pass.");
    }
    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    if (orientation == Qt::Horizontal)
    {
        if (section == 0)
        {
            return QString("Instruction Type");
        }
        else if (section == 1)
        {
            return QString("Count");
        }
    }
    return QVariant();
}


QVariant InstructionTypeCountModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    if (role == Qt::TextAlignmentRole)
    {
        return static_cast<Column>(index.column()) == Column::InstructionCount
                   ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                   : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    }

    if (role == Qt::FontRole)
    {
        return tableNumericFont();
    }

    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    Column column = static_cast<Column>(index.column());
    if (column == Column::InstructionType)
    {
        return QString::fromStdString(
            instr::instructionTypeName(static_cast<instr::InstructionType>(index.row())));
        
    }
    else if (column == Column::InstructionCount)
    {
        // Return the count
        const auto row    = static_cast<size_t>(index.row());
        const auto counts = m_source ? m_source() : InstructionTypeCounts{};
        return row < counts.size() ? QString::number(counts[row]) : QVariant();
    }

    return QVariant();
}

void InstructionTypeCountModel::updateInstructionTypeCountsSlot()
{
    beginResetModel();
    endResetModel();
}

void InstructionTypeCountModel::countsChangedSlot()
{
    // Only the numbers move; the rows (the types) never do.
    emit dataChanged(index(0, 1), index(rowCount() - 1, 1), {Qt::DisplayRole});
}
} // namespace Atlas
