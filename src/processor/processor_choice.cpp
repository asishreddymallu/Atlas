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
    if (!pipeline)
    {
        return "Every instruction finishes in one cycle; there are no pipeline hazards.";
    }
    if (hazard && forwarding)
    {
        // rv5s_hdu.cpp:82-104 - with forwarding on, the only stall is
        // the load-use one; everything else is handled by the four
        // forwarding paths.
        return "The pipeline stalls one cycle on a load-use hazard and forwards everything else.";
    }
    if (hazard)
    {
        // rv5s_hdu.cpp:106-117 - two stalls then one, i.e. the dependent
        // instruction waits until the producer has reached write-back.
        return "Without forwarding, a result is only available after write-back; the pipeline "
               "stalls until then.";
    }
    if (forwarding)
    {
        // rv5s_processor_nh_f.cpp never calls check_data_hazard, so the
        // load-use case the forwarding unit cannot cover is not caught
        // either. Measured in known_issues 77: a dependent chain comes
        // out as 0 on rv5-nh-f as well as rv5-nh-nf.
        return "Without hazard detection the pipeline never stalls, so dependent instructions "
               "need nops between them.";
    }
    return "Without hazard detection the pipeline never stalls, and without forwarding a result "
           "is only available after write-back: dependent instructions need nops between them.";
}

} // namespace Kites
