#include "memory_tab.h"
#include "ui/common/data_table_style.h"
#include "ui_memory_tab.h"
namespace Kites
{
namespace
{
// The combo box's three entries, as the .ui spells them. One table instead
// of two if-chains, so the label and the base can never drift apart.
struct BaseEntry
{
    const char *text;
    Base        base;
};
constexpr BaseEntry baseEntries[] = {
    {"Hex", Base::Hexadecimal},
    {"Binary", Base::Binary},
    {"Decimal", Base::Decimal},
};

Base baseForText(const QString &text)
{
    for (const BaseEntry &entry : baseEntries)
        if (text == QLatin1String(entry.text))
            return entry.base;
    return Base::Hexadecimal; // the combo's first entry, and the model's default
}
} // namespace

MemoryTab::MemoryTab(QWidget *parent, MemoryController *memoryController)
    : KitesTab(parent), ui(new Ui::MemoryTab),
      m_memoryModel(new MemoryModel(this, memoryController))
{
    ui->setupUi(this);
    ui->memoryTableView->setModel(m_memoryModel);
    // every column is numeric, so the headers read right too
    applyDataTableStyle(ui->memoryTableView, Qt::AlignRight | Qt::AlignVCenter);
    ui->memoryTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QRegularExpression re("0x[0-9a-fA-F]+$");
    QRegularExpressionValidator *validator = new QRegularExpressionValidator(re, this);
    ui->searchBar->setValidator(validator);

    // connect(m_ui->Me)
    m_memoryModel->setRowsVisible(1);
    connect(ui->memoryTableView, &MemoryTableView::scrolled, m_memoryModel,
            [this](bool dir)
            {
                if (dir)
                {
                    m_memoryModel->offsetCentralAddress(1);
                }
                else
                {
                    m_memoryModel->offsetCentralAddress(-1);
                }
            });
    connect(ui->memoryTableView, &MemoryTableView::resized, m_memoryModel,
            [=, this]
            {
                int rowHeight = ui->memoryTableView->rowHeight(0);
                if (rowHeight != 0)
                {
                    const auto rows = ui->memoryTableView->height() / rowHeight;
                    m_memoryModel->setRowsVisible(rows);
                }
            });

    connect(ui->searchButton, &QPushButton::clicked, this,
            [this]
            {
                QString searchText = ui->searchBar->text();
                // int displayType = ui->displayTypeComboBox->currentIndex();
                m_memoryModel->setCentralAddress(searchText.toULongLong(nullptr, 16));
            });
    connect(ui->comboBox, &QComboBox::currentTextChanged, this,
            [this](const QString &text)
            {
                const Base base = baseForText(text);
                m_memoryModel->setDisplayBase(base);
                // Announced after the model has it, so anything that reacts
                // sees the table already in the new base.
                emit displayBaseChangedSignal(base);
            });
}

void MemoryTab::triggerSearch()
{
    ui->searchButton->click();
}

void MemoryTab::setDisplayBase(Base base)
{
    for (const BaseEntry &entry : baseEntries)
        if (entry.base == base)
            ui->comboBox->setCurrentText(QLatin1String(entry.text));
}

Base MemoryTab::displayBase() const
{
    return baseForText(ui->comboBox->currentText());
}

void MemoryTab::changeMemoryController(MemoryController *memoryController)
{
    m_memoryModel->changeMemoryController(memoryController);
}

MemoryTab::~MemoryTab()
{
    delete ui;
}
} // namespace Kites
