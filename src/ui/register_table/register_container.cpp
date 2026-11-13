#include "register_container.h"
#include "ui/common/data_table_style.h"
#include "ui_register_container.h"
#include "processor/registers.h"
#include "ui/common/collapsible_section.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <array>

namespace Kites
{
namespace
{
// Filters a RegisterModel down to a contiguous row range, so General
// Purpose / Floating Point registers can be shown in separate collapsible
// sections while sharing the same underlying model.
class RegisterRangeProxyModel : public QSortFilterProxyModel
{
  public:
    RegisterRangeProxyModel(int firstRow, int lastRowExclusive, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent), m_firstRow(firstRow), m_lastRowExclusive(lastRowExclusive)
    {
    }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        Q_UNUSED(sourceParent);
        return sourceRow >= m_firstRow && sourceRow < m_lastRowExclusive;
    }

  private:
    int m_firstRow;
    int m_lastRowExclusive;
};

// The combo box's three entries, as the .ui spells them. One table instead
// of an if-chain, so the label and the base cannot drift apart.
struct BaseEntry
{
    const char *text;
    Base        base;
};
constexpr BaseEntry baseEntries[] = {
    {"Binary", Base::Binary},
    {"Hexadecimal", Base::Hexadecimal},
    {"Decimal/Float", Base::Decimal},
};

Base baseForText(const QString &text)
{
    for (const BaseEntry &entry : baseEntries)
        if (text == QLatin1String(entry.text))
            return entry.base;
    return Base::Hexadecimal; // what the constructor selects, and the model's default
}
} // namespace

RegisterContainer::RegisterContainer(QWidget *parent, RegisterFile *regfile)
    : QWidget(parent), ui(new Ui::RegisterContainer),
      m_registerModel(new RegisterModel(this, regfile))
{
    ui->setupUi(this);
    setupRegisterTable();

    ui->displayType->setCurrentText("Hexadecimal");
    connect(ui->displayType, &QComboBox::currentTextChanged, this,
            [this](const QString &text)
            {
                const Base base = baseForText(text);
                m_registerModel->setDisplayBase(base);
                // Announced after the model has it, so anything that reacts
                // sees the table already in the new base.
                emit displayBaseChangedSignal(base);
            });
}

QSortFilterProxyModel *RegisterContainer::makeRangeProxy(int firstRow, int lastRowExclusive)
{
    auto *proxy = new RegisterRangeProxyModel(firstRow, lastRowExclusive, this);
    proxy->setSourceModel(m_registerModel);
    return proxy;
}

void RegisterContainer::setupRegisterTable()
{
    auto *sectionsLayout = new QVBoxLayout(ui->sectionsContainer);
    sectionsLayout->setContentsMargins(0, 0, 0, 0);
    sectionsLayout->setSpacing(4);

    struct SectionSpec
    {
        QString title;
        int firstRow;
        int lastRowExclusive;
    };
    const std::array<SectionSpec, 2> sections = {
        SectionSpec{"General Purpose", 0, 32},
        SectionSpec{"Floating Point", 32, 64},
    };

    for (const SectionSpec &spec : sections)
    {
        auto *section = new CollapsibleSection(spec.title, ui->sectionsContainer);

        auto *tableView = new QTableView(section);
        tableView->setModel(makeRangeProxy(spec.firstRow, spec.lastRowExclusive));
        applyDataTableStyle(tableView);
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        auto* header = tableView->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::Stretch);

        section->setContentWidget(tableView);
        sectionsLayout->addWidget(section);
    }
}

void RegisterContainer::setDisplayBase(Base base)
{
    for (const BaseEntry &entry : baseEntries)
        if (entry.base == base)
            ui->displayType->setCurrentText(QLatin1String(entry.text));
}

Base RegisterContainer::displayBase() const
{
    return baseForText(ui->displayType->currentText());
}

void RegisterContainer::setRegisterFile(RegisterFile *regfile)
{
    m_registerModel->changeRegisterFile(regfile);
}

RegisterContainer::~RegisterContainer()
{
    delete ui;
}

} // namespace Kites
