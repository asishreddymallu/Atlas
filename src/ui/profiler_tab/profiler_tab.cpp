#include "profiler_tab.h"
#include "ui/common/data_table_style.h"
#include "ui_profiler_tab.h"
#include "profiler/profiler.h"
#include "common/instruction_types.h"
#include "instruction_type_count_model.h"
#include "processor/processor_manager.h"
#include "ui/common/processor_metrics_model.h"
#include <QHeaderView>
#include <QLineEdit>
#include <QTableView>
#include <QVBoxLayout>

namespace Atlas
{
ProfilerTab::ProfilerTab(QWidget *parent, const Profiler *profiler)
    : AtlasTab(parent), ui(new Ui::ProfilerTab), m_profiler(profiler)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(0, 1);
    
    ui->instructionTypeCountTableView->horizontalHeader()->
    setSectionResizeMode(QHeaderView::Stretch);
    applyDataTableStyle(ui->instructionTypeCountTableView);
    ui->plainTextEdit->setPlaceholderText(
        "No profile yet.\n\n"
        "Run a program (F5) to see per-line execution counts,\n"
        "instruction types and an execution heat map.");
    if (m_profiler)
    {   
        // The table sums the margin's per-line counts by type: the run, as
        // Instructions retired counts it, not each source line once (T4).
        ProfilerEditor            *margin = ui->plainTextEdit;
        InstructionTypeCountModel *model  = new InstructionTypeCountModel(
            this, [margin]() { return margin->executedTypeCounts(); });
        connect(margin, &ProfilerEditor::marginChangedSignal, model,
                &InstructionTypeCountModel::countsChangedSlot);
        ui->instructionTypeCountTableView->setModel(model);
        // connect(m_profiler, &Profiler::lineExecutionCountsUpdated, this,
        //         &ProfilerTab::updateLineExecutionCounts);
        connect(m_profiler, &Profiler::profilerReset, this,
                &ProfilerTab::resetProfilerView);
        connect(m_profiler, &Profiler::lineExecutionCountChangedSignal, ui->plainTextEdit,
                &ProfilerEditor::setLineExecutionCountSlot);
        connect(m_profiler, &Profiler::updateLineInstructionTypeSignal, ui->plainTextEdit,
                &ProfilerEditor::updateLineInstructionTypeSlot);
        connect(m_profiler, &Profiler::updateInstructionTypeCountsSignal, model,
                &InstructionTypeCountModel::updateInstructionTypeCountsSlot);
    }
}

void ProfilerTab::setProcessorManager(ProcessorManager *manager)
{
    if (!manager || m_metricsModel)
        return;
    m_metricsModel = new ProcessorMetricsModel(this, manager);
    auto *view     = new QTableView(this);
    view->setObjectName("profilerMetricsTableView");
    view->setModel(m_metricsModel);
    // Value and Source (WP-05's [Measured]/[Modelled]) are sized to their
    // contents, so a number and its tag are always whole; the name takes
    // what is left of this narrow pane and elides last. With the name sized
    // to contents the third column pushed Value out of view.
    view->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    view->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    view->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    applyDataTableStyle(view);
    ui->verticalLayout_3->insertWidget(0, view);
    connect(manager, &ProcessorManager::processorStateChangedSignal, m_metricsModel,
            &ProcessorMetricsModel::refresh);
}

ProfilerTab::~ProfilerTab()
{
    delete ui;
}

void ProfilerTab::setSourceText(const QString &sourceText)
{
    ui->plainTextEdit->setPlainText(sourceText);
    ui->plainTextEdit->clearExecutionCount();
}

void ProfilerTab::updateLineExecutionCount(const std::map<int, int> &lineExecutionCounts)
{
    ui->plainTextEdit->setExecutionCount(lineExecutionCounts);
}

void ProfilerTab::updateLineInstructionType(const std::map<int, std::string> &lineInstructionTypes)
{
    ui->plainTextEdit->setInstructionTypes(lineInstructionTypes);
}

void ProfilerTab::updateStatistics([[maybe_unused]] const std::map<std::string, int> &statistics)
{
    
}

void ProfilerTab::resetProfilerView()
{
    // Was empty (known_issues 85). The source text stays: on a reload it
    // is replaced anyway, and after a switch it is still the program in
    // the editor.
    ui->plainTextEdit->clearExecutionCount();
    ui->plainTextEdit->clearInstructionTypes();
}
} // namespace Atlas
