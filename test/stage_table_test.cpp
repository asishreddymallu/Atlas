/**
 * @file stage_table_test.cpp
 * @brief The circuit view's stage table shows what each pipeline register
 *        holds.
 *
 * The scene has always had a one-row QTableWidget with a column per
 * pipeline register (IF/ID, ID/EX, EX/MEM, MEM/WB) or one CI column for
 * single-cycle (circuit_scene.cpp, fitTableToCircuit), and nothing ever
 * filled it. Each cell now holds the register's instruction, disassembled
 * by instruction_set::disassemble, with the PC in the tooltip; a NOP or
 * bubble reads "bubble" in grey.
 *
 * atlas_test does not link the compiled .qrc, so a processor's own
 * loadScene(":/circuit_designs/...") finds nothing there; each test loads
 * the same drawing from disk (ctest runs in test/) through the public
 * loadScene, which is the path the app takes with the resource.
 *
 * The expected text is computed from the pipeline registers read directly
 * through a Probe subclass - not from stageCells(), which is the code under
 * test - and disassembled here.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include "common/instructions.h"
#include "ui/processor_tab/circuit_scene.h"

#include <QCoreApplication>
#include <QTableWidget>
#include <QTemporaryDir>

#include <fstream>
#include <string>

using namespace Atlas;
using namespace Atlas::test_support;

namespace
{
template <class P> struct Probe : P
{
    using P::ex_mem_reg_;
    using P::id_ex_reg_;
    using P::if_id_reg_;
    using P::mem_wb_reg_;
};

int serial = 0;

std::string writeProgram(const QTemporaryDir &dir, const std::string &body)
{
    const std::string path = dir.path().toStdString() + "/tb" + std::to_string(serial++) + ".s";
    std::ofstream     file(path);
    file << ".text\n" << body;
    return path;
}

struct Expected
{
    QString text;
    QString tip;
};

Expected expectedFor(uint32_t instruction, uint64_t pc)
{
    if (instruction == NOP || pc == INVALID_PC)
        return {"bubble", pc == INVALID_PC
                              ? QString("bubble")
                              : QString("bubble (nop) at PC 0x%1").arg(pc, 8, 16, QLatin1Char('0'))};
    return {QString::fromStdString(instruction_set::disassemble(instruction)),
            QString("PC 0x%1").arg(pc, 8, 16, QLatin1Char('0'))};
}

/// Loads the drawing from the source tree; see the file comment.
void loadDrawing(ProcessorBase &vm, const char *file)
{
    vm.circuit_scene_->loadScene(QStringLiteral("../resources/circuit_designs/") +
                                 QString::fromLatin1(file));
}

std::string cellText(QTableWidget *table, int column)
{
    QTableWidgetItem *item = table->item(0, column);
    return item ? item->text().toStdString() : std::string("(no item)");
}
} // namespace

TEST(StageTable, EachCellHoldsItsPipelineRegistersInstructionAfterEveryStep)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    setupVmStateDirectory();
    Probe<RV5StageProcessorHF> vm;
    // A load and its use: h_f stalls one cycle and puts a bubble in ID/EX.
    vm.LoadProgram(assemble(writeProgram(
        dir, "    addi t0, x0, 5\n    sd t0, 256(x0)\n    ld t1, 256(x0)\n"
             "    add t2, t1, t1\n    sub t3, t2, t0\n")));
    loadDrawing(vm, "H_F_Processor.json");
    QTableWidget *table = vm.circuit_scene_->instructionTable();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->columnCount(), 4);
    const char *headers[] = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};
    for (int c = 0; c < 4; ++c)
        EXPECT_EQ(table->horizontalHeaderItem(c)->text().toStdString(), headers[c]);

    int instructions = 0;
    for (int cycle = 1; cycle <= 14; ++cycle)
    {
        static_cast<ProcessorBase &>(vm).StepAndNotify();
        const std::pair<uint32_t, uint64_t> regs[] = {
            {vm.if_id_reg_.instruction, vm.if_id_reg_.pc},
            {vm.id_ex_reg_.instruction, vm.id_ex_reg_.pc},
            {vm.ex_mem_reg_.instruction, vm.ex_mem_reg_.pc},
            {vm.mem_wb_reg_.instruction, vm.mem_wb_reg_.pc}};
        for (int c = 0; c < 4; ++c)
        {
            const Expected want = expectedFor(regs[c].first, regs[c].second);
            QTableWidgetItem *item = table->item(0, c);
            ASSERT_NE(item, nullptr) << "cycle " << cycle << " column " << headers[c];
            EXPECT_EQ(item->text(), want.text) << "cycle " << cycle << ", " << headers[c];
            EXPECT_EQ(item->toolTip(), want.tip) << "cycle " << cycle << ", " << headers[c];
            if (want.text == "bubble")
            {
                EXPECT_EQ(item->foreground().color(), QColor(128, 128, 128))
                    << "a bubble must be drawn dimmed, cycle " << cycle;
            }
            else
                ++instructions;
        }
    }
    EXPECT_GT(instructions, 10) << "the table never showed real instructions";
}

TEST(StageTable, ALoadUseStallShowsABubbleInIdEx)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    setupVmStateDirectory();
    Probe<RV5StageProcessorHF> vm;
    vm.LoadProgram(assemble(writeProgram(dir, "    ld t1, 256(x0)\n    add t2, t1, t1\n")));
    loadDrawing(vm, "H_F_Processor.json");
    QTableWidget *table = vm.circuit_scene_->instructionTable();

    // Cycle 1: ld in ID. Cycle 2: ld in EX, add in ID - the hazard unit
    // stalls in cycle 3, so after it ID/EX is a bubble and add is still in
    // IF/ID.
    bool sawStallBubble = false;
    for (int cycle = 1; cycle <= 5; ++cycle)
    {
        static_cast<ProcessorBase &>(vm).StepAndNotify();
        if (vm.id_ex_reg_.instruction == NOP &&
            (vm.if_id_reg_.instruction & 0x7F) == 0b0110011) // add still waiting in ID
        {
            EXPECT_EQ(cellText(table, 1), "bubble") << "cycle " << cycle;
            EXPECT_EQ(cellText(table, 0), instruction_set::disassemble(vm.if_id_reg_.instruction))
                << "cycle " << cycle;
            sawStallBubble = true;
        }
    }
    EXPECT_TRUE(sawStallBubble) << "the load-use stall never happened, so nothing was checked";
}

TEST(StageTable, SingleCycleShowsOneCellWithTheInstructionJustExecuted)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    setupVmStateDirectory();
    RVSSProcessor vm;
    const AssembledProgram program =
        assemble(writeProgram(dir, "    addi t0, x0, 5\n    add t1, t0, t0\n"));
    vm.LoadProgram(program);
    loadDrawing(vm, "single_cycle_draft1.json");
    QTableWidget *table = vm.circuit_scene_->instructionTable();
    ASSERT_EQ(table->columnCount(), 1);
    EXPECT_EQ(table->horizontalHeaderItem(0)->text().toStdString(), "CI");
    EXPECT_EQ(cellText(table, 0), "bubble") << "before any step";

    for (size_t i = 0; i < program.text_buffer.size(); ++i)
    {
        static_cast<ProcessorBase &>(vm).StepAndNotify();
        // RVSS connects its scene with Qt::QueuedConnection.
        QCoreApplication::processEvents();
        EXPECT_EQ(cellText(table, 0), instruction_set::disassemble(program.text_buffer[i]))
            << "after step " << i + 1;
        EXPECT_EQ(table->item(0, 0)->toolTip(),
                  QString("PC 0x%1").arg(i * 4, 8, 16, QLatin1Char('0')));
    }
}
