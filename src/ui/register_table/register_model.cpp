#include "register_model.h"
#include "ui/common/table_font.h"
#include "ui/theme/theme_manager.h"
#include "common/register_names.h"
#include <QApplication>
#include <QBrush>
#include <QDebug>
#include <QPalette>

namespace Kites
{

RegisterModel::RegisterModel(QObject *parent, RegisterFile *regfile) : QAbstractTableModel(parent)
{
    m_currentRegisterFile = regfile;
    connect(m_currentRegisterFile, &RegisterFile::updateRegister, this,
            &RegisterModel::updateRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateFRegister, this,
            &RegisterModel::updateFRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::registerResetSignal, this,
            &RegisterModel::registerResetSlot);
}

int RegisterModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 64;
}

int RegisterModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 3;
} // Register name, alias, value

void RegisterModel::setDisplayBase(Base base)
{
    beginResetModel();
    m_displayBase = base;
    endResetModel();
}

QVariant RegisterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant{};

    if (orientation == Qt::Horizontal)
    {
        switch (section)
        {
        case 0:
            return QString("Register");
        case 1:
            return QString("Alias");
        case 2:
            return QString("Value");
        }
    }

    return QVariant{};
}

QVariant RegisterModel::data(const QModelIndex &index, int role) const
{
    if (!m_currentRegisterFile)
        return QVariant{};

    if (role == Qt::TextAlignmentRole)
    {
        // identifiers read left, the numeric value reads right so a column of
        // hex can be scanned down its digits
        return index.column() == 2
                   ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                   : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    }
    if (role == Qt::FontRole)
    {
        // The "just changed" row tint has to stay at 25% alpha to keep the
        // row's own text above 4.5:1, which makes it deliberately subtle.
        // Bolding the value that actually changed carries the same signal
        // without depending on colour at all. Value column only: bolding the
        // whole row would compete with the tint and make the panel noisy
        // during a run.
        //
        // Safe for both bundled fonts - neither reflows the column. IBM Plex
        // Mono ships a real Bold face; Monaco has none, so Qt synthesises it,
        // and a render test showed the synthesised bold adds 41.9% ink while
        // shifting the right edge by 0 px.
        if (index.column() == 2 &&
            static_cast<size_t>(index.row()) == m_highlightedRegisterIndex)
        {
            QFont highlighted = tableNumericFont();
            highlighted.setBold(true);
            return highlighted;
        }
        return tableNumericFont();
    }
    if (role == Qt::BackgroundRole)
    {
        if (static_cast<size_t>(index.row()) == m_highlightedRegisterIndex)
        {
            // "this register just changed" tint. Was a hard-coded #b13232fd
            // (ARGB: 69% over a blue); the info token carries the same meaning
            // and 25% keeps the row's text above 4.5:1 in every theme, which
            // 69% did not (1.89:1 in tokyo_night_storm).
            QColor highlight = ThemeManager::getInstance().getInfoColor();
            highlight.setAlpha(64);
            return QBrush(highlight);
        }
    }
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
    {
        size_t row = static_cast<size_t>(index.row());
        switch (index.column())
        {
        case 0: // Register Name
            if (row < 32)
            {
                return QString("x%1").arg(row);
            }
            else
            {
                return QString("f%1").arg(row - 32);
            }
        case 1: // Register Alias
        {
            if (row < 32)
                return QString(register_names::kGprAliases[row]);
            else if (row < 64)
                return QString(register_names::kFprAliases[row - 32]);
            else
                return QString("");
        }
        case 2: // Register Value
        {
            if (row < 32)
            {
                uint64_t value = m_currentRegisterFile->ReadGpr(row);
                switch (m_displayBase)
                {
                case Base::Binary:
                    return QString("0b%1").arg(QString::number(value, 2));
                case Base::Decimal:
                    return QString::number(static_cast<int64_t>(value));
                case Base::Hexadecimal:
                    return QString("0x%1").arg(QString::number(value, 16).toUpper());
                }
            }
            else if (row < 64)
            {
                int fIndex = row - 32;

                // read raw bits of FP register (you must have this function)
                uint64_t raw = m_currentRegisterFile->ReadFpr(fIndex);

                switch (m_displayBase)
                {
                case Base::Binary:
                {
                    return QString("0b%1").arg(QString::number(raw, 2));
                }

                case Base::Decimal:
                {
                    uint32_t upper32 = static_cast<uint32_t>(raw >> 32);
                    if (upper32 == 0)
                    {
                        // Single-precision float (F-extension): value in lower 32 bits
                        uint32_t lower32 = static_cast<uint32_t>(raw & 0xFFFFFFFF);
                        float fval;
                        memcpy(&fval, &lower32, sizeof(float));
                        return QString::number(static_cast<double>(fval), 'g', 8);
                    }
                    else
                    {
                        // Double-precision (D-extension): full 64 bits
                        double dval;
                        memcpy(&dval, &raw, sizeof(double));
                        return QString::number(dval, 'g', 16);
                    }
                }

                case Base::Hexadecimal:
                {
                    return QString("0x%1").arg(QString::number(raw, 16).toUpper());
                }
                }
            }
            else
            {
                return QString("");
            } // TODO : add floating point register support
            break;
        }
        default:
            return QVariant{};
        }
    }
    return QVariant{};
}

void RegisterModel::changeRegisterFile(RegisterFile *regfile)
{
    beginResetModel();
    m_currentRegisterFile = regfile;
    connect(m_currentRegisterFile, &RegisterFile::updateRegister, this,
            &RegisterModel::updateRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateFRegister, this,
            &RegisterModel::updateFRegisterValue);
    // maybe we can directly connect it to
    connect(m_currentRegisterFile, &RegisterFile::registerResetSignal, this,
            &RegisterModel::registerResetSlot);
    endResetModel();
}

void RegisterModel::registerResetSlot()
{
    beginResetModel();
    endResetModel();
}

void RegisterModel::updateRegisterValue(size_t regIndex, [[maybe_unused]] uint64_t value)
{
    //QModelIndex index = this->index(static_cast<int>(regIndex), 2);
    m_highlightedRegisterIndex = regIndex; // Update highlighted register index
    // emit dataChanged(index,index);
    beginResetModel();
    endResetModel();
}

void RegisterModel::updateFRegisterValue(size_t regIndex, [[maybe_unused]] uint64_t value)
{
    //QModelIndex index = this->index(static_cast<int>(regIndex + 32), 2);
    m_highlightedRegisterIndex = regIndex + 32; // Update highlighted register index
    // emit dataChanged(index,index);
    beginResetModel();
    endResetModel();
}
} // namespace Kites
