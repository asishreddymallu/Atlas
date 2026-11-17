/**
 * File Name: alu.cpp
 * Author: Vishank Singh
 * Github: https://github.com/VishankSingh
 */

#include "processor/alu.h"
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Kites
{
namespace alu
{

[[nodiscard]] std::pair<uint64_t, bool> Alu::execute(AluOp op, uint64_t a, uint64_t b)
{
    switch (op)
    {
    case AluOp::ADD:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        int64_t result = sa + sb;
        bool overflow = __builtin_add_overflow(sa, sb, &result);
        return {static_cast<uint64_t>(result), overflow};
    }
    case AluOp::ADDW:
    {
        auto sa = static_cast<int32_t>(a);
        auto sb = static_cast<int32_t>(b);
        int32_t result = sa + sb;
        bool overflow = __builtin_add_overflow(sa, sb, &result);
        return {static_cast<uint64_t>(static_cast<int32_t>(result)), overflow};
    }
    case AluOp::SUB:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        int64_t result = sa - sb;
        bool overflow = __builtin_sub_overflow(sa, sb, &result);
        return {static_cast<uint64_t>(result), overflow};
    }
    case AluOp::SUBW:
    {
        auto sa = static_cast<int32_t>(a);
        auto sb = static_cast<int32_t>(b);
        int32_t result = sa - sb;
        bool overflow = __builtin_sub_overflow(sa, sb, &result);
        return {static_cast<uint64_t>(static_cast<int32_t>(result)), overflow};
    }
    case AluOp::MUL:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        int64_t result = sa * sb;
        bool overflow = __builtin_mul_overflow(sa, sb, &result);
        return {static_cast<uint64_t>(result), overflow};
    }
    case AluOp::MULH:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        // TODO: do something about this, msvc doesnt support __int128

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        __int128 result = static_cast<__int128>(sa) * static_cast<__int128>(sb);
        auto high_result = static_cast<int64_t>(result >> 64);
#pragma GCC diagnostic pop

        return {static_cast<uint64_t>(high_result), false};
    }
    case AluOp::MULHSU:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<uint64_t>(b);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        __int128 result = static_cast<__int128>(sa) * static_cast<__int128>(sb);
        auto high_result = static_cast<int64_t>(result >> 64);
#pragma GCC diagnostic pop

        return {static_cast<uint64_t>(high_result), false};
    }
    case AluOp::MULHU:
    {
        auto ua = static_cast<uint64_t>(a);
        auto ub = static_cast<uint64_t>(b);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        __int128 result = static_cast<__int128>(ua) * static_cast<__int128>(ub);
        auto high_result = static_cast<int64_t>(result >> 64);
#pragma GCC diagnostic pop

        return {static_cast<uint64_t>(high_result), false};
    }
    case AluOp::MULW:
    {
        auto sa = static_cast<int32_t>(a);
        auto sb = static_cast<int32_t>(b);
        int64_t result = static_cast<int64_t>(sa) * static_cast<int64_t>(sb);
        auto lower_result = static_cast<int32_t>(result);
        bool overflow = (result != static_cast<int64_t>(static_cast<int32_t>(result)));
        return {static_cast<uint64_t>(lower_result), overflow};
    }
    case AluOp::DIV:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        if (sb == 0)
        {
            return {static_cast<uint64_t>(-1), false};
        }
        if (sa == INT64_MIN && sb == -1)
        {
            return {static_cast<uint64_t>(INT64_MIN), true};
        }
        int64_t result = sa / sb;
        return {static_cast<uint64_t>(result), false};
    }
    case AluOp::DIVW:
    {
        auto sa = static_cast<int32_t>(a);
        auto sb = static_cast<int32_t>(b);
        if (sb == 0)
        {
            return {static_cast<uint64_t>(static_cast<int64_t>(-1)), false};
        }
        if (sa == INT32_MIN && sb == -1)
        {
            return {static_cast<uint64_t>(static_cast<int64_t>(INT32_MIN)), true};
        }
        int32_t result = sa / sb;
        return {static_cast<uint64_t>(static_cast<int64_t>(result)), false};
    }
    case AluOp::DIVU:
    {
        if (b == 0)
        {
            return {UINT64_MAX, false};
        }
        uint64_t result = a / b;
        return {result, false};
    }
    case AluOp::DIVUW:
    {
        auto ua = static_cast<uint32_t>(a);
        auto ub = static_cast<uint32_t>(b);
        if (ub == 0)
        {
            return {static_cast<uint64_t>(static_cast<int64_t>(-1)), false};
        }
        auto result = static_cast<int32_t>(ua / ub);
        return {static_cast<uint64_t>(static_cast<int64_t>(result)), false};
    }
    case AluOp::REM:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        if (sb == 0)
        {
            return {static_cast<uint64_t>(sa), false};
        }
        if (sa == INT64_MIN && sb == -1)
        {
            return {0, true};
        }
        int64_t result = sa % sb;
        return {static_cast<uint64_t>(result), false};
    }
    case AluOp::REMW:
    {
        auto sa = static_cast<int32_t>(a);
        auto sb = static_cast<int32_t>(b);
        if (sb == 0)
        {
            return {static_cast<uint64_t>(static_cast<int64_t>(sa)), false};
        }
        if (sa == INT32_MIN && sb == -1)
        {
            return {0, true};
        }
        int32_t result = sa % sb;
        return {static_cast<uint64_t>(static_cast<int64_t>(result)), false};
    }
    case AluOp::REMU:
    {
        if (b == 0)
        {
            return {a, false};
        }
        uint64_t result = a % b;
        return {result, false};
    }
    case AluOp::REMUW:
    {
        auto ua = static_cast<uint32_t>(a);
        auto ub = static_cast<uint32_t>(b);
        if (ub == 0)
        {
            return {static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(ua))), false};
        }
        auto result = static_cast<int32_t>(ua % ub);
        return {static_cast<uint64_t>(static_cast<int64_t>(result)), false};
    }
    case AluOp::AND:
    {
        return {static_cast<uint64_t>(a & b), false};
    }
    case AluOp::OR:
    {
        return {static_cast<uint64_t>(a | b), false};
    }
    case AluOp::XOR:
    {
        return {static_cast<uint64_t>(a ^ b), false};
    }
    case AluOp::SLL:
    {
        uint64_t result = a << (b & 63);
        return {result, false};
    }
    case AluOp::SLLW:
    {
        auto sa = static_cast<uint32_t>(a);
        auto sb = static_cast<uint32_t>(b);
        uint32_t result = sa << (sb & 31);
        return {static_cast<uint64_t>(static_cast<int32_t>(result)), false};
    }
    case AluOp::SRL:
    {
        uint64_t result = a >> (b & 63);
        return {result, false};
    }
    case AluOp::SRLI:
    {
        uint64_t result = a >> (b & 63);
        return {result, false};
    }
    case AluOp::SRLW:
    {
        auto sa = static_cast<uint32_t>(a);
        auto sb = static_cast<uint32_t>(b);
        uint32_t result = sa >> (sb & 31);
        return {static_cast<uint64_t>(static_cast<int32_t>(result)), false};
    }
    case AluOp::SRA:
    {
        auto sa = static_cast<int64_t>(a);
        return {static_cast<uint64_t>(sa >> (b & 63)), false};
    }
    case AluOp::SRAI:
    {
        auto sa = static_cast<int64_t>(a);
        return {static_cast<uint64_t>(sa >> (b & 63)), false};
    }
    case AluOp::SRAW:
    {
        auto sa = static_cast<int32_t>(a);
        return {static_cast<uint64_t>(sa >> (b & 31)), false};
    }
    case AluOp::SLT:
    {
        auto sa = static_cast<int64_t>(a);
        auto sb = static_cast<int64_t>(b);
        return {static_cast<uint64_t>(sa < sb), false};
    }
    case AluOp::SLTU:
    {
        return {static_cast<uint64_t>(a < b), false};
    }
    default:
        return {0, false};
    }
}

namespace
{
/// The canonical NaN of the RISC-V F extension: positive, quiet bit set,
/// payload zero. A single is returned in the low 32 bits, zero-extended,
/// which is this simulator's convention for a single everywhere (the
/// spec NaN-boxes instead; see docs/audit/known_issues.md).
constexpr uint64_t kCanonicalNaN32 = 0x7FC00000ULL;
constexpr uint64_t kCanonicalNaN64 = 0x7FF8000000000000ULL;

bool isNaNBits32(uint64_t bits)
{
    const auto word = static_cast<uint32_t>(bits);
    return ((word >> 23) & 0xFF) == 0xFF && (word & 0x7FFFFF) != 0;
}
bool isNaNBits64(uint64_t bits)
{
    return ((bits >> 52) & 0x7FF) == 0x7FF && (bits & 0xFFFFFFFFFFFFFULL) != 0;
}

/**
 * @brief The ops whose NaN result must be the canonical NaN.
 *
 * An ALLOW-list, not an exempt list, and the difference matters. The two
 * functions carry 60 case labels between them (29 in fpexecute, 31 in
 * dfpexecute), which leave by three exits:
 *
 *     37  the shared tail's float packing
 *      7  the tail's is_int_result return
 *     16  an early return, reaching neither point
 *
 * So 44 ops reach the tail. **These 24 canonicalize**: 23 of them at the
 * float exit, plus FCVT_S_D at the is_int_result exit, where it goes
 * because its result is a 32-bit single rather than a double. A measured
 * sweep of 306 NaN cases on five models found these are exactly the ops
 * that ever produce a NaN.
 *
 * The 20 that reach the tail and do NOT canonicalize are, at the float
 * exit, the six fsgnj/fsgnjn/fsgnjx - which must keep an operand's
 * payload and sign, and are the reason an exempt list would be fragile -
 * and the eight integer-to-float conversions, which cannot produce a NaN
 * at all; and at the is_int_result exit, the six feq/flt/fle, whose
 * results are integers.
 *
 * Naming the producers means a new op added to the tail canonicalizes
 * nothing until someone decides it should. The other way round, it would
 * silently start rewriting results.
 *
 * Not here, and not by accident: the 16 early returns are fcvt.w/wu/l/lu
 * (eight), fclass (two), the four fmv forms, and the two ADD labels the
 * address path reuses. None of them is in this list, and the one place
 * that canonicalizes an is_int_result value tests for FCVT_S_D
 * explicitly, so feq/flt/fle cannot be caught by it.
 */
bool canonicalizesNaN(AluOp op)
{
    switch (op)
    {
    case AluOp::FADD_S:   case AluOp::FADD_D:
    case AluOp::FSUB_S:   case AluOp::FSUB_D:
    case AluOp::FMUL_S:   case AluOp::FMUL_D:
    case AluOp::FDIV_S:   case AluOp::FDIV_D:
    case AluOp::FSQRT_S:  case AluOp::FSQRT_D:
    case AluOp::FMIN_S:   case AluOp::FMIN_D:
    case AluOp::FMAX_S:   case AluOp::FMAX_D:
    case AluOp::FMADD_S:  case AluOp::FMADD_D:
    case AluOp::FMSUB_S:  case AluOp::FMSUB_D:
    case AluOp::FNMADD_S: case AluOp::FNMADD_D:
    case AluOp::FNMSUB_S: case AluOp::FNMSUB_D:
    case AluOp::FCVT_S_D: case AluOp::FCVT_D_S:
        return true;
    default:
        return false;
    }
}
} // namespace

[[nodiscard]] std::pair<uint64_t, uint8_t> Alu::fpexecute(AluOp op, uint64_t ina, uint64_t inb,
                                                          uint64_t inc, uint8_t rm)
{
    float a, b, c;
    std::memcpy(&a, &ina, sizeof(float));
    std::memcpy(&b, &inb, sizeof(float));
    std::memcpy(&c, &inc, sizeof(float));
    float result = 0.0;
    uint64_t int_result = 0;
    bool is_int_result = false;
    // int_result is used for instructions like feq flt
    // where the result is either 0 or 1

    uint8_t fcsr = 0;

    int original_rm = std::fegetround();

    switch (rm)
    {
    case 0b000:
        std::fesetround(FE_TONEAREST);
        break; // RNE
    case 0b001:
        std::fesetround(FE_TOWARDZERO);
        break; // RTZ
    case 0b010:
        std::fesetround(FE_DOWNWARD);
        break; // RDN
    case 0b011:
        std::fesetround(FE_UPWARD);
        break; // RUP
               // 0b100 RMM, unsupported
    default:
        break;
    }

    std::feclearexcept(FE_ALL_EXCEPT);

    switch (op)
    {
    case AluOp::ADD:
    {
        auto sa = static_cast<int64_t>(ina);
        auto sb = static_cast<int64_t>(inb);
        int64_t res = sa + sb;
        // bool overflow = __builtin_add_overflow(sa, sb, &res); // TODO: check this
        return {static_cast<uint64_t>(res), 0};
    }
    case AluOp::FMADD_S:
    {
        result = std::fma(a, b, c);
        break;
    }
    case AluOp::FMSUB_S:
    {
        result = std::fma(a, b, -c);
        break;
    }
    case AluOp::FNMADD_S:
    {
        result = std::fma(-a, b, -c);
        break;
    }
    case AluOp::FNMSUB_S:
    {
        result = std::fma(-a, b, c);
        break;
    }
    case AluOp::FADD_S:
    {
        result = a + b;
        break;
    }
    case AluOp::FSUB_S:
    {
        result = a - b;
        break;
    }
    case AluOp::FMUL_S:
    {
        result = a * b;
        break;
    }
    case AluOp::FDIV_S:
    {
        if (b == 0.0f)
        {
            result = std::numeric_limits<float>::quiet_NaN();
            fcsr |= FCSR_DIV_BY_ZERO;
        }
        else
        {
            result = a / b;
        }
        break;
    }
    case AluOp::FSQRT_S:
    {
        if (a < 0.0f)
        {
            result = std::numeric_limits<float>::quiet_NaN();
            fcsr |= FCSR_INVALID_OP;
        }
        else
        {
            result = std::sqrt(a);
        }
        break;
    }
    case AluOp::FCVT_W_S:
    {
        if (!std::isfinite(a) || a > static_cast<float>(INT32_MAX) ||
            a < static_cast<float>(INT32_MIN))
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            auto res = static_cast<int64_t>(static_cast<int32_t>(a > 0 ? INT32_MAX : INT32_MIN));
            return {static_cast<uint64_t>(res), fcsr};
        }
        else
        {
            auto ires = static_cast<int32_t>(std::nearbyint(a));
            auto res = static_cast<int64_t>(ires); // sign-extend
            fesetround(original_rm);
            return {static_cast<uint64_t>(res), fcsr};
        }
        break;
    }
    case AluOp::FCVT_WU_S:
    {
        if (!std::isfinite(a) || a > static_cast<float>(UINT32_MAX) || a < 0.0f)
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            uint32_t saturate = (a < 0.0f) ? 0 : UINT32_MAX;
            auto res = static_cast<int64_t>(static_cast<int32_t>(saturate)); // sign-extend
            return {static_cast<uint64_t>(res), fcsr};
        }
        else
        {
            auto ires = static_cast<uint32_t>(std::nearbyint(a));
            auto res = static_cast<int64_t>(static_cast<int32_t>(ires)); // sign-extend
            fesetround(original_rm);
            return {static_cast<uint64_t>(res), fcsr};
        }
        break;
    }
    case AluOp::FCVT_L_S:
    {
        if (!std::isfinite(a) || a > static_cast<float>(INT64_MAX) ||
            a < static_cast<float>(INT64_MIN))
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            int64_t saturate = (a < 0.0f) ? INT64_MIN : INT64_MAX;
            return {static_cast<uint64_t>(saturate), fcsr};
        }
        else
        {
            auto ires = static_cast<int64_t>(std::nearbyint(a));
            fesetround(original_rm);
            return {static_cast<uint64_t>(ires), fcsr};
        }
        break;
    }
    case AluOp::FCVT_LU_S:
    {
        if (!std::isfinite(a) || a > static_cast<float>(UINT64_MAX) || a < 0.0f)
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            uint64_t saturate = (a < 0.0f) ? 0 : UINT64_MAX;
            return {saturate, fcsr};
        }
        else
        {
            auto ires = static_cast<uint64_t>(std::nearbyint(a));
            fesetround(original_rm);
            return {ires, fcsr};
        }
        break;
    }
    case AluOp::FCVT_S_W:
    {
        auto ia = static_cast<int32_t>(ina);
        result = static_cast<float>(ia);
        break;
    }
    case AluOp::FCVT_S_WU:
    {
        auto ua = static_cast<uint32_t>(ina);
        result = static_cast<float>(ua);
        break;
    }
    case AluOp::FCVT_S_L:
    {
        auto la = static_cast<int64_t>(ina);
        result = static_cast<float>(la);
        break;
    }
    case AluOp::FCVT_S_LU:
    {
        auto ula = static_cast<uint64_t>(ina);
        result = static_cast<float>(ula);
        break;
    }
    case AluOp::FSGNJ_S:
    {
        auto a_bits = static_cast<uint32_t>(ina);
        auto b_bits = static_cast<uint32_t>(inb);
        uint32_t temp = (a_bits & 0x7FFFFFFF) | (b_bits & 0x80000000);
        std::memcpy(&result, &temp, sizeof(float));
        break;
    }
    case AluOp::FSGNJN_S:
    {
        auto a_bits = static_cast<uint32_t>(ina);
        auto b_bits = static_cast<uint32_t>(inb);
        uint32_t temp = (a_bits & 0x7FFFFFFF) | (~b_bits & 0x80000000);
        std::memcpy(&result, &temp, sizeof(float));
        break;
    }
    case AluOp::FSGNJX_S:
    {
        auto a_bits = static_cast<uint32_t>(ina);
        auto b_bits = static_cast<uint32_t>(inb);
        uint32_t temp = (a_bits & 0x7FFFFFFF) | ((a_bits ^ b_bits) & 0x80000000);
        std::memcpy(&result, &temp, sizeof(float));
        break;
    }
    case AluOp::FMIN_S:
    {
        if (std::isnan(a) && !std::isnan(b))
        {
            result = b;
        }
        else if (!std::isnan(a) && std::isnan(b))
        {
            result = a;
        }
        else if (std::signbit(a) != std::signbit(b) && a == b)
        {
            result = -0.0f; // Both zero but with different signs — return -0.0
        }
        else
        {
            result = std::fmin(a, b);
        }
        break;
    }
    case AluOp::FMAX_S:
    {
        if (std::isnan(a) && !std::isnan(b))
        {
            result = b;
        }
        else if (!std::isnan(a) && std::isnan(b))
        {
            result = a;
        }
        else if (std::signbit(a) != std::signbit(b) && a == b)
        {
            result = 0.0f; // Both zero but with different signs — return +0.0
        }
        else
        {
            result = std::fmax(a, b);
        }
        break;
    }
    case AluOp::FEQ_S:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a == b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FLT_S:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a < b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FLE_S:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a <= b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FCLASS_S:
    {
        auto a_bits = static_cast<uint32_t>(ina);
        float af;
        std::memcpy(&af, &a_bits, sizeof(float));
        uint16_t res = 0;

        if (std::signbit(af) && std::isinf(af))
            res |= 1 << 0; // -inf
        else if (std::signbit(af) && std::fpclassify(af) == FP_NORMAL)
            res |= 1 << 1; // -normal
        else if (std::signbit(af) && std::fpclassify(af) == FP_SUBNORMAL)
            res |= 1 << 2; // -subnormal
        else if (std::signbit(af) && af == 0.0f)
            res |= 1 << 3; // -zero
        else if (!std::signbit(af) && af == 0.0f)
            res |= 1 << 4; // +zero
        else if (!std::signbit(af) && std::fpclassify(af) == FP_SUBNORMAL)
            res |= 1 << 5; // +subnormal
        else if (!std::signbit(af) && std::fpclassify(af) == FP_NORMAL)
            res |= 1 << 6; // +normal
        else if (!std::signbit(af) && std::isinf(af))
            res |= 1 << 7; // +inf
        else if (std::isnan(af) && (a_bits & 0x00400000) == 0)
            res |= 1 << 8; // signaling NaN
        else if (std::isnan(af))
            res |= 1 << 9; // quiet NaN

        std::fesetround(original_rm);
        return {res, fcsr};
    }
    case AluOp::FMV_X_W:
    {
        int32_t float_bits;
        std::memcpy(&float_bits, &ina, sizeof(float));
        auto sign_extended = static_cast<int64_t>(float_bits);
        return {static_cast<uint64_t>(sign_extended), fcsr};
    }
    case AluOp::FMV_W_X:
    {
        auto int_bits = static_cast<uint32_t>(ina & 0xFFFFFFFF);
        std::memcpy(&result, &int_bits, sizeof(float));
        break;
    }
    default:
        break;
    }

    int raised = std::fetestexcept(FE_ALL_EXCEPT);
    if (raised & FE_INVALID)
        fcsr |= FCSR_INVALID_OP;
    if (raised & FE_DIVBYZERO)
        fcsr |= FCSR_DIV_BY_ZERO;
    if (raised & FE_OVERFLOW)
        fcsr |= FCSR_OVERFLOW;
    if (raised & FE_UNDERFLOW)
        fcsr |= FCSR_UNDERFLOW;
    if (raised & FE_INEXACT)
        fcsr |= FCSR_INEXACT;

    std::fesetround(original_rm);
    if (is_int_result)
    {
        return {int_result, fcsr};
    }
    uint32_t result_bits = 0;
    std::memcpy(&result_bits, &result, sizeof(result));
    uint64_t out = static_cast<uint64_t>(result_bits);
    if (canonicalizesNaN(op) && isNaNBits32(out))
        out = kCanonicalNaN32;
    return {out, fcsr};
}

[[nodiscard]] std::pair<uint64_t, bool> Alu::dfpexecute(AluOp op, uint64_t ina, uint64_t inb,
                                                        uint64_t inc, uint8_t rm)
{
    double a, b, c;
    std::memcpy(&a, &ina, sizeof(double));
    std::memcpy(&b, &inb, sizeof(double));
    std::memcpy(&c, &inc, sizeof(double));
    double result = 0.0;
    uint64_t int_result = 0;
    bool is_int_result = false;
    // int_result is used for instructions like feq flt
    // where the result is either 0 or 1

    uint8_t fcsr = 0;

    int original_rm = std::fegetround();

    switch (rm)
    {
    case 0b000:
        std::fesetround(FE_TONEAREST);
        break; // RNE
    case 0b001:
        std::fesetround(FE_TOWARDZERO);
        break; // RTZ
    case 0b010:
        std::fesetround(FE_DOWNWARD);
        break; // RDN
    case 0b011:
        std::fesetround(FE_UPWARD);
        break; // RUP
               // 0b100 RMM, unsupported
    default:
        break;
    }

    std::feclearexcept(FE_ALL_EXCEPT);

    switch (op)
    {
    case AluOp::ADD:
    {
        auto sa = static_cast<int64_t>(ina);
        auto sb = static_cast<int64_t>(inb);
        int64_t res = sa + sb;
        // bool overflow = __builtin_add_overflow(sa, sb, &res); // TODO: check this
        return {static_cast<uint64_t>(res), 0};
    }
    case AluOp::FMADD_D:
    {
        result = std::fma(a, b, c);
        break;
    }
    case AluOp::FMSUB_D:
    {
        result = std::fma(a, b, -c);
        break;
    }
    case AluOp::FNMADD_D:
    {
        result = std::fma(-a, b, -c);
        break;
    }
    case AluOp::FNMSUB_D:
    {
        result = std::fma(-a, b, c);
        break;
    }
    case AluOp::FADD_D:
    {
        result = a + b;
        break;
    }
    case AluOp::FSUB_D:
    {
        result = a - b;
        break;
    }
    case AluOp::FMUL_D:
    {
        result = a * b;
        break;
    }
    case AluOp::FDIV_D:
    {
        if (b == 0.0)
        {
            result = std::numeric_limits<double>::quiet_NaN();
            fcsr |= FCSR_DIV_BY_ZERO;
        }
        else
        {
            result = a / b;
        }
        break;
    }
    case AluOp::FSQRT_D:
    {
        if (a < 0.0)
        {
            result = std::numeric_limits<double>::quiet_NaN();
            fcsr |= FCSR_INVALID_OP;
        }
        else
        {
            result = std::sqrt(a);
        }
        break;
    }
    case AluOp::FCVT_W_D:
    {
        if (!std::isfinite(a) || a > static_cast<double>(INT32_MAX) ||
            a < static_cast<double>(INT32_MIN))
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            int32_t saturate = (a < 0.0) ? INT32_MIN : INT32_MAX;
            auto res = static_cast<int64_t>(saturate); // sign-extend to XLEN
            return {static_cast<uint64_t>(res), fcsr};
        }
        else
        {
            auto ires = static_cast<int32_t>(std::nearbyint(a));
            auto res = static_cast<int64_t>(ires); // sign-extend to XLEN
            fesetround(original_rm);
            return {static_cast<uint64_t>(res), fcsr};
        }
        break;
    }
    case AluOp::FCVT_WU_D:
    {
        if (!std::isfinite(a) || a > static_cast<double>(UINT32_MAX) || a < 0.0)
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            uint32_t saturate = (a < 0.0) ? 0 : UINT32_MAX;
            auto res = static_cast<int64_t>(static_cast<int32_t>(saturate)); // sign-extend per spec
            return {static_cast<uint64_t>(res), fcsr};
        }
        else
        {
            auto ires = static_cast<uint32_t>(std::nearbyint(a));
            auto res = static_cast<int64_t>(static_cast<int32_t>(ires)); // sign-extend
            fesetround(original_rm);
            return {static_cast<uint64_t>(res), fcsr};
        }
        break;
    }
    case AluOp::FCVT_L_D:
    {
        if (!std::isfinite(a) || a > static_cast<double>(INT64_MAX) ||
            a < static_cast<double>(INT64_MIN))
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            int64_t saturate = (a < 0.0) ? INT64_MIN : INT64_MAX;
            return {static_cast<uint64_t>(saturate), fcsr};
        }
        else
        {
            auto ires = static_cast<int64_t>(std::nearbyint(a));
            fesetround(original_rm);
            return {static_cast<uint64_t>(ires), fcsr};
        }
        break;
    }
    case AluOp::FCVT_LU_D:
    {
        if (!std::isfinite(a) || a > static_cast<double>(UINT64_MAX) || a < 0.0)
        {
            fcsr |= FCSR_INVALID_OP;
            fesetround(original_rm);
            uint64_t saturate = (a < 0.0) ? 0 : UINT64_MAX;
            return {saturate, fcsr};
        }
        else
        {
            auto ires = static_cast<uint64_t>(std::nearbyint(a));
            fesetround(original_rm);
            return {ires, fcsr};
        }
        break;
    }
    case AluOp::FCVT_D_W:
    {
        auto ia = static_cast<int32_t>(ina);
        result = static_cast<double>(ia);
        break;
    }
    case AluOp::FCVT_D_WU:
    {
        auto ua = static_cast<uint32_t>(ina);
        result = static_cast<double>(ua);
        break;
    }
    case AluOp::FCVT_D_L:
    {
        auto la = static_cast<int64_t>(ina);
        result = static_cast<double>(la);
        break;
    }
    case AluOp::FCVT_D_LU:
    {
        auto ula = static_cast<uint64_t>(ina);
        result = static_cast<double>(ula);
        break;
    }
    case AluOp::FSGNJ_D:
    {
        auto a_bits = static_cast<uint64_t>(ina);
        auto b_bits = static_cast<uint64_t>(inb);
        uint64_t temp = (a_bits & 0x7FFFFFFFFFFFFFFF) | (b_bits & 0x8000000000000000);
        std::memcpy(&result, &temp, sizeof(double));
        break;
    }
    case AluOp::FSGNJN_D:
    {
        auto a_bits = static_cast<uint64_t>(ina);
        auto b_bits = static_cast<uint64_t>(inb);
        uint64_t temp = (a_bits & 0x7FFFFFFFFFFFFFFF) | (~b_bits & 0x8000000000000000);
        std::memcpy(&result, &temp, sizeof(double));
        break;
    }
    case AluOp::FSGNJX_D:
    {
        auto a_bits = static_cast<uint64_t>(ina);
        auto b_bits = static_cast<uint64_t>(inb);
        uint64_t temp = (a_bits & 0x7FFFFFFFFFFFFFFF) | ((a_bits ^ b_bits) & 0x8000000000000000);
        std::memcpy(&result, &temp, sizeof(double));
        break;
    }
    case AluOp::FMIN_D:
    {
        if (std::isnan(a) && !std::isnan(b))
        {
            result = b;
        }
        else if (!std::isnan(a) && std::isnan(b))
        {
            result = a;
        }
        else if (std::signbit(a) != std::signbit(b) && a == b)
        {
            result = -0.0; // Both zero but with different signs — return -0.0
        }
        else
        {
            result = std::fmin(a, b);
        }
        break;
    }
    case AluOp::FMAX_D:
    {
        if (std::isnan(a) && !std::isnan(b))
        {
            result = b;
        }
        else if (!std::isnan(a) && std::isnan(b))
        {
            result = a;
        }
        else if (std::signbit(a) != std::signbit(b) && a == b)
        {
            result = 0.0; // Both zero but with different signs — return +0.0
        }
        else
        {
            result = std::fmax(a, b);
        }
        break;
    }
    case AluOp::FEQ_D:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a == b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FLT_D:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a < b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FLE_D:
    {
        is_int_result = true;
        if (std::isnan(a) || std::isnan(b))
        {
            int_result = 0;
        }
        else
        {
            int_result = (a <= b) ? 1 : 0;
        }
        break;
    }
    case AluOp::FCLASS_D:
    {
        uint64_t a_bits = ina;
        double af;
        std::memcpy(&af, &a_bits, sizeof(double));
        uint16_t res = 0;

        if (std::signbit(af) && std::isinf(af))
            res |= 1 << 0; // -inf
        else if (std::signbit(af) && std::fpclassify(af) == FP_NORMAL)
            res |= 1 << 1; // -normal
        else if (std::signbit(af) && std::fpclassify(af) == FP_SUBNORMAL)
            res |= 1 << 2; // -subnormal
        else if (std::signbit(af) && af == 0.0)
            res |= 1 << 3; // -zero
        else if (!std::signbit(af) && af == 0.0)
            res |= 1 << 4; // +zero
        else if (!std::signbit(af) && std::fpclassify(af) == FP_SUBNORMAL)
            res |= 1 << 5; // +subnormal
        else if (!std::signbit(af) && std::fpclassify(af) == FP_NORMAL)
            res |= 1 << 6; // +normal
        else if (!std::signbit(af) && std::isinf(af))
            res |= 1 << 7; // +inf
        else if (std::isnan(af) && (a_bits & 0x0008000000000000) == 0)
            res |= 1 << 8; // signaling NaN
        else if (std::isnan(af))
            res |= 1 << 9; // quiet NaN

        std::fesetround(original_rm);
        return {res, fcsr};
    }
    case AluOp::FCVT_D_S:
    {
        // The SOURCE is a single, in the low 32 bits of the register.
        // static_cast<float>(ina) converted the bit pattern as an
        // integer - 1.5f, 0x3FC00000, became 1067450368.0 (known_issues
        // 57). `a` is no use either: it is `ina` read as a double.
        auto     bits = static_cast<uint32_t>(ina & 0xFFFFFFFF);
        float    fa   = 0.0F;
        std::memcpy(&fa, &bits, sizeof(fa));
        result = static_cast<double>(fa);
        break;
    }
    case AluOp::FCVT_S_D:
    {
        // The SOURCE is a double - that is `a`, which the function
        // already memcpy'd - and the RESULT is a single, so it cannot
        // fall through to the shared tail, which memcpy's eight bytes of
        // `double`. A single occupies the low 32 bits of an f register,
        // zero-extended, exactly as Alu::fpexecute returns it.
        const float fr   = static_cast<float>(a);
        uint32_t    bits = 0;
        std::memcpy(&bits, &fr, sizeof(bits));
        // Through int_result, not an early return, so the shared tail
        // still collects the inexact/overflow/underflow flags this
        // conversion can raise and still restores the rounding mode.
        int_result    = static_cast<uint64_t>(bits);
        is_int_result = true;
        break;
    }
    case AluOp::FMV_D_X:
    {
        uint64_t double_bits;
        std::memcpy(&double_bits, &ina, sizeof(double));
        return {double_bits, fcsr};
    }
    case AluOp::FMV_X_D:
    {
        uint64_t int_bits = ina & 0xFFFFFFFFFFFFFFFF;
        double out;
        std::memcpy(&out, &int_bits, sizeof(double));
        result = out;
        break;
    }
    default:
        break;
    }

    int raised = std::fetestexcept(FE_ALL_EXCEPT);
    if (raised & FE_INVALID)
        fcsr |= FCSR_INVALID_OP;
    if (raised & FE_DIVBYZERO)
        fcsr |= FCSR_DIV_BY_ZERO;
    if (raised & FE_OVERFLOW)
        fcsr |= FCSR_OVERFLOW;
    if (raised & FE_UNDERFLOW)
        fcsr |= FCSR_UNDERFLOW;
    if (raised & FE_INEXACT)
        fcsr |= FCSR_INEXACT;

    std::fesetround(original_rm);
    if (is_int_result)
    {
        // Every op that leaves here carries an INTEGER result - feq, flt,
        // fle - except fcvt.s.d, which the single/double split sends this
        // way because its result is a 32-bit single rather than the
        // double the exit below packs. So this is the one place where an
        // is_int_result value is a floating-point value, and the test is
        // for that op by name.
        if (op == AluOp::FCVT_S_D && isNaNBits32(int_result))
        {
            return {kCanonicalNaN32, fcsr};
        }
        return {int_result, fcsr};
    }

    uint64_t result_bits = 0;
    std::memcpy(&result_bits, &result, sizeof(result));
    if (canonicalizesNaN(op) && isNaNBits64(result_bits))
        result_bits = kCanonicalNaN64;
    return {result_bits, fcsr};
}

void Alu::setFlags(bool carry, bool zero, bool negative, bool overflow)
{
    carry_ = carry;
    zero_ = zero;
    negative_ = negative;
    overflow_ = overflow;
}

} // namespace alu
}//namespace Kites