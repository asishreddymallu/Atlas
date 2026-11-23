#pragma once
#include <array>
#include <functional>
#include <QAbstractTableModel>
#include "common/instruction_types.h"
#include "utils/to_index.h"
namespace Atlas
{


class InstructionTypeCountModel : public QAbstractTableModel
{
    using InstructionTypeCounts = std::array<size_t, 
    toIndex(instruction_set::InstructionType::INSTRUCTION_TYPE_COUNT)>;
    public:
    /// The counts are read through `source` each time they are shown.
    InstructionTypeCountModel(QObject *parent, std::function<InstructionTypeCounts()> source);
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    public slots:
    void updateInstructionTypeCountsSlot();
    void countsChangedSlot();
    
    private:

    enum class Column
    {
        InstructionType = 0,
        InstructionCount = 1
    };
    // The model only displays the counts; it never keeps its own.
    std::function<InstructionTypeCounts()> m_source;
};  
} // namespace Atlas

