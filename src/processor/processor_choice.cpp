/**
 * @file processor_choice.cpp
 * @brief Implementation of the chooser's mapping and its wording.
 */
#include "processor/processor_choice.h"

namespace Kites
{

ProcessorType processorTypeFor(bool pipeline, bool hazard, bool forwarding)
{
    if (!pipeline)
    {
        // The single-cycle model has no pipeline, so neither box applies.
        return ProcessorType::RVSS;
    }
    if (hazard)
    {
        return forwarding ? ProcessorType::RV5Stage_H_F : ProcessorType::RV5Stage_H_NF;
    }
    return forwarding ? ProcessorType::RV5Stage_NH_F : ProcessorType::RV5Stage_NH_NF;
}

ProcessorType processorTypeFor(const ProcessorChoice &choice)
{
    return processorTypeFor(choice.pipeline, choice.hazard, choice.forwarding);
}

ProcessorChoice processorChoiceFor(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RVSS:
        // Not "no hazard detection, no forwarding": the booleans are
        // free here, and these are what the greyed boxes keep, so this
        // is where ticking Pipeline lands.
        return {false, true, true};
    case ProcessorType::RV5Stage_NH_NF:
        return {true, false, false};
    case ProcessorType::RV5Stage_H_NF:
        return {true, true, false};
    case ProcessorType::RV5Stage_NH_F:
        return {true, false, true};
    case ProcessorType::RV5Stage_H_F:
        return {true, true, true};
    case ProcessorType::ProcessorTypeCount:
        break;
    }
    return {false, true, true};
}

const char *processorDescription(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RVSS:
        return "Single-cycle";
    case ProcessorType::RV5Stage_NH_NF:
        return "Pipeline, no hazard detection, no forwarding";
    case ProcessorType::RV5Stage_H_NF:
        return "Pipeline, hazard detection, no forwarding";
    case ProcessorType::RV5Stage_NH_F:
        return "Pipeline, no hazard detection, forwarding";
    case ProcessorType::RV5Stage_H_F:
        return "Pipeline, hazard detection + forwarding";
    case ProcessorType::ProcessorTypeCount:
        break;
    }
    return "unknown processor";
}

const char *processorNote(bool pipeline, bool hazard, bool forwarding)
{
    // Every number below is measured, on every test run, by
    // test/processor_note_measure_test.cpp: a dependent pair with 0..4 nops
    // between producer and consumer (ALU, load and FP producers), and li/la
    // run unpadded. That test fails if a number here stops matching.
    if (!pipeline)
    {
        return "Every instruction finishes in one cycle; there are no pipeline hazards.";
    }
    if (hazard && forwarding)
    {
        return "Forwards results, and stalls one cycle when a load is used right away.\n"
               "Nothing to pad.";
    }
    if (hazard)
    {
        return "No forwarding: stalls until the producer reaches write-back.\n"
               "Nothing to pad.";
    }
    if (forwarding)
    {
        return "Forwards results but never stalls: a load needs 1 nop before its use.\n"
               "Other dependent pairs need no nops.";
    }
    return "No stalls, no forwarding: a value is ready at write-back, so its reader needs 2 nops.\n"
           "li of a large value and la give wrong results here.";
}

} // namespace Kites
