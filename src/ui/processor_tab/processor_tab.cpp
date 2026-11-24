#include "processor_tab.h"
#include "circuit_scene.h"
#include "circuit_view.h"
#include "ui_processor_tab.h"
#include <QHBoxLayout>

// #include "vsrtl/graphics/vsrtl_widget.h"
namespace Atlas
{
ProcessorTab::ProcessorTab(QWidget *parent, ProcessorManager *vmManager)
    : AtlasTab(parent), ui(new Ui::ProcessorTab), m_vmManager(vmManager)
{
    ui->setupUi(this);
    // The diagram has the whole tab (final/polish). The run counters that
    // used to sit under it (PC, cycles, retired, CPI, IPC, stalls,
    // mispredictions) are all in the Profiler tab, with their labels.
    ui->gridLayout->setContentsMargins(0, 0, 0, 0);
    ui->graphicsView->showScene(m_vmManager->getCircuitScene());
}

void ProcessorTab::setWiresStayActive(bool stayActive)
{
    auto scene = m_vmManager->getCircuitScene();
    if (scene)
    {
        scene->setWireStayActive(stayActive);
    }
}

void ProcessorTab::onVMChanged()
{
    // The vm has been change so we ge the circuit scene of the
    // current vm and set it to the graphics view
    bool stayActive = m_vmManager->getCircuitScene()->getWireStayActive();
    ui->graphicsView->showScene(m_vmManager->getCircuitScene()); // a new processor: back to Fit
    m_vmManager->getCircuitScene()->setWireStayActive(stayActive);
    // keep the wire stay active setting
}

ProcessorTab::~ProcessorTab()
{
    delete ui;
}
} // namespace Atlas
