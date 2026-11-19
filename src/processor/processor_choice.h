/**
 * @file processor_choice.h
 * @brief The five models as three check marks, and what to say about them.
 *
 * The chooser asks two questions - single-cycle or pipeline, and which of
 * hazard detection and forwarding - and the answer has to become one of
 * the five `ProcessorType` values. That relation used to live inside the
 * dialog as a comparison against five literal strings, with the reverse
 * direction done by indexing a tree with `static_cast<int>` of the enum,
 * so it could not be exercised without building a dialog.
 *
 * It lives here instead: free functions, no Qt, no widget, nothing to
 * construct. The dialog reads them and the status bar reads them, and
 * `test/processor_choice_test.cpp` pins every combination.
 */
#pragma once

#include "processor/processor_types.h"

namespace Kites
{
/**
 * @brief A processor as the chooser's three controls describe it.
 *
 * `hazard` and `forwarding` mean nothing when `pipeline` is false - the
 * single-cycle model has no pipeline to have hazards in - but they are
 * still carried, because the dialog greys the two boxes rather than
 * clearing them and has to put something back when the user returns to
 * Pipeline.
 */
struct ProcessorChoice
{
    bool pipeline   = false;
    bool hazard     = false;
    bool forwarding = false;
};

/**
 * @brief The model those three controls mean.
 *
 * `pipeline == false` is `RVSS` whatever the other two say.
 */
ProcessorType processorTypeFor(bool pipeline, bool hazard, bool forwarding);

/// The same, from a whole choice.
ProcessorType processorTypeFor(const ProcessorChoice &choice);

/**
 * @brief How the chooser should show `type`.
 *
 * The inverse of `processorTypeFor`: feeding this back into it returns
 * `type` for all five values. For `RVSS` the two booleans are free, and
 * the pair returned is the one the user most likely wants if they then
 * tick Pipeline - hazard detection and forwarding both on, rather than
 * the variant that needs hand-written nops (known_issues 77).
 */
ProcessorChoice processorChoiceFor(ProcessorType type);

/// The processor named for a human, as the status bar shows it.
const char *processorDescription(ProcessorType type);

/**
 * @brief At most two short lines on what this combination costs the
 *        programmer: whether it stalls or forwards, and how many nops a
 *        dependent instruction needs.
 *
 * The nop counts are measured, not reasoned: test/
 * processor_note_measure_test.cpp runs dependent pairs at every distance
 * on each pipeline and fails if the note stops matching. The chooser shows
 * it, and the status bar's processor label carries it as its tooltip.
 */
const char *processorNote(bool pipeline, bool hazard, bool forwarding);

} // namespace Kites
