#include <gtest/gtest.h>
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "assembler/assembler.h"
#include "utils/utils.h"

using namespace Atlas;

// pipeline_test_code.s (restored from 58f9c8d) is addi x1,x1,1 ... addi x1,x1,8.
// nh-nf has no hazard unit and no forwarding: each addi reads x1 in ID while the two
// before it are still in EX and MEM, and sees the value written by the one three ahead
// (WB writes before ID reads). So x1 = 1, 2, 3, 5, 7, 9, 12, 15 - not the sum, 36.
TEST(FIVE_STAGE_VM_TEST,no_hazard_no_forwarding_test_1)
{
    setupVmStateDirectory();
    RV5StageProcessorNHNF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(1), 15);
}

TEST(FIVE_STAGE_VM_TEST,no_hazard_no_forwarding_test_2)
{
    setupVmStateDirectory();
    RV5StageProcessorNHNF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code_2.s");
    vm.LoadProgram(program);
    vm.DebugRun();    
    
    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(4), 20);
}

// h-nf stalls each dependent addi until x1 is written back, so the sum is exact.
TEST(FIVE_STAGE_VM_TEST,hazard_no_forwarding_test_1)
{
    setupVmStateDirectory();
    RV5StageProcessorHNF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(1), 36);
}

// The three tests below asserted nothing; each now checks the sum. Their names are kept
// as they were (including the spelling); the model each one builds is named in a comment.

// nh-f forwards EX->EX, which is all a chain of addi needs.
TEST(FIVE_STAGE_VM_TEST,no_hazard_forwarding_test)
{
    setupVmStateDirectory();
    RV5StageProcessorNHF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(1), 36);
}

// Builds h-f (hazard unit and forwarding), whatever the name says.
TEST(FIVE_STAGE_VM_TEST,harzard_no_forwarding_test)
{
    setupVmStateDirectory();
    RV5StageProcessorHF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(1), 36);
}

// Builds h-nf (hazard unit, no forwarding), whatever the name says.
TEST(FIVE_STAGE_VM_TEST,harzard_forwarding_test)
{
    setupVmStateDirectory();
    RV5StageProcessorHNF vm;
    AssembledProgram program = assemble("../examples/pipeline_test_code.s");
    vm.LoadProgram(program);
    vm.DebugRun();

    GTEST_ASSERT_EQ(vm.registers_.ReadGpr(1), 36);
}
