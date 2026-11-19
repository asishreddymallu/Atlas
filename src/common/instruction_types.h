/**
 * @brief This file contains the definition of the InstructionType enum class,
 * along with enum to string mapping
 * 
 */

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include "utils/to_index.h"
namespace Kites
{
/*TODO
* Make the enum value name consistent across the code base 
* Either it shoudl follo ALL_CAPS or PascalCase
* Also we can use magic_enum library to avoid writing the mapping manually
* but this would do for now
*/
namespace instruction_set
{
enum class InstructionType 
{
    R_TYPE = 0,
    I_TYPE,
    S_TYPE,
    B_TYPE,
    U_TYPE,
    J_TYPE,
    F_TYPE,
    CSR_TYPE, // Zicsr: csrrw, csrrs, csrrc and their immediate forms
    // Inside the count, so it has a slot and a name like every other
    // type. It used to sit past INSTRUCTION_TYPE_COUNT, and every
    // array indexed by type was one slot short for it; a CSR
    // instruction, which classified as UNKNOWN, aborted Run.
    UNKNOWN,
    INSTRUCTION_TYPE_COUNT
};

inline constexpr size_t kInstructionTypeCount = toIndex(InstructionType::INSTRUCTION_TYPE_COUNT);

inline const std::array<std::string, kInstructionTypeCount> instructionTypeNames =
{
    "R-Type", 
    "I-Type", 
    "S-Type", 
    "B-Type", 
    "U-Type", 
    "J-Type", 
    "F-Type",
    "CSR",
    "Other"
};

/// The display name of a type. Total: a value outside the enum (from a
/// cast, say) reads "Other" instead of indexing past the table.
inline const std::string &instructionTypeName(InstructionType type)
{
    const size_t index = toIndex(type);
    return instructionTypeNames[index < kInstructionTypeCount ? index : toIndex(InstructionType::UNKNOWN)];
}
}// namespace instruction_set
}// namespace Kites
