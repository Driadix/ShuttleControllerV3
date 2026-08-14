// Crash-marker format tests (design docs/safety-authority-design-v3.md section 7.3;
// T24, T25). Одно 32-bit слово (payload + CRC16, атомарный store); permissive
// CRC-fail; round-trip без снятия (read-only на стартапе), clear - только явный ack.
#include <gtest/gtest.h>

#include "domain/marker_format.h"

namespace
{

using v3::safety::MarkerState;
using v3::safety::marker_crc16;
using v3::safety::marker_decode;
using v3::safety::marker_encode;

TEST(MarkerFormat, RoundTripPreservesState)
{
    // Без краха.
    const std::uint32_t w0 = marker_encode(MarkerState{});
    const MarkerState s0 = marker_decode(w0);
    EXPECT_FALSE(s0.crash_pending);
    EXPECT_EQ(s0.crash_count, 0u);

    // С крахом + счётчик.
    const std::uint32_t w1 = marker_encode(MarkerState{true, 42u});
    const MarkerState s1 = marker_decode(w1);
    EXPECT_TRUE(s1.crash_pending);
    EXPECT_EQ(s1.crash_count, 42u);
}

// T24: Повреждённый маркер (бит-флип CRC) -> decode = {crash_pending: false}
// (permissive, решение владельца: первый boot BKP-мусор; однословный атомарный
// store делает CRC-fail после init-штампа недостижимым).
TEST(MarkerFormat, DamagedMarkerIsPermissive)
{
    const std::uint32_t good = marker_encode(MarkerState{true, 7u});
    const std::uint32_t damaged = good ^ 0x00010000u; // флип в CRC-поле
    const MarkerState s = marker_decode(damaged);
    EXPECT_FALSE(s.crash_pending);
    EXPECT_EQ(s.crash_count, 0u);
}

// T25: Чтение не снимает маркер (read-only на стартапе, Q5 A); clear - явный ack.
TEST(MarkerFormat, ReadDoesNotClear)
{
    const std::uint32_t w = marker_encode(MarkerState{true, 3u});
    // Два чтения подряд (повторный boot): состояние сохраняется.
    EXPECT_TRUE(marker_decode(w).crash_pending);
    EXPECT_TRUE(marker_decode(w).crash_pending);
    // Снятие - только явный reset-error ack (адаптер clear_crash); формат этого
    // не делает сам - пере-энкод пустого состояния даёт "no crash".
    EXPECT_FALSE(marker_decode(marker_encode(MarkerState{})).crash_pending);
}

TEST(MarkerFormat, CrcDetectsPayloadCorruption)
{
    const std::uint32_t w = marker_encode(MarkerState{true, 0x1234u});
    const std::uint32_t corrupted = w ^ 0x00000001u; // флип бита payload
    EXPECT_FALSE(marker_decode(corrupted).crash_pending);
}

TEST(MarkerFormat, CrcDeterministic)
{
    EXPECT_EQ(marker_crc16(0u), marker_crc16(0u));
    EXPECT_NE(marker_crc16(0u), marker_crc16(1u));
}

} // namespace
