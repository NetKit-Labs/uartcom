#pragma once

// USART register map for STM32F4 (RM0390, USART chapter).
// The driver writes these bits directly. It does not call or wrap the STM32 HAL.

#include <cstddef>
#include <cstdint>

namespace uartcom::reg {

inline constexpr std::uint32_t kSrPe = 1u << 0;
inline constexpr std::uint32_t kSrFe = 1u << 1;
inline constexpr std::uint32_t kSrNf = 1u << 2;
inline constexpr std::uint32_t kSrOre = 1u << 3;
inline constexpr std::uint32_t kSrIdle = 1u << 4;
inline constexpr std::uint32_t kSrRxne = 1u << 5;
inline constexpr std::uint32_t kSrTc = 1u << 6;
inline constexpr std::uint32_t kSrTxe = 1u << 7;
inline constexpr std::uint32_t kSrLbd = 1u << 8;
inline constexpr std::uint32_t kSrCts = 1u << 9;
inline constexpr std::uint32_t kSrLineErrors = kSrPe | kSrFe | kSrNf;

inline constexpr std::uint32_t kCr1Re = 1u << 2;
inline constexpr std::uint32_t kCr1Te = 1u << 3;
inline constexpr std::uint32_t kCr1Idleie = 1u << 4;
inline constexpr std::uint32_t kCr1Rxneie = 1u << 5;
inline constexpr std::uint32_t kCr1Tcie = 1u << 6;
inline constexpr std::uint32_t kCr1Txeie = 1u << 7;
inline constexpr std::uint32_t kCr1Peie = 1u << 8;
inline constexpr std::uint32_t kCr1Ps = 1u << 9;
inline constexpr std::uint32_t kCr1Pce = 1u << 10;
inline constexpr std::uint32_t kCr1M = 1u << 12;
inline constexpr std::uint32_t kCr1Ue = 1u << 13;
inline constexpr std::uint32_t kCr1Over8 = 1u << 15;

inline constexpr std::uint32_t kCr2StopShift = 12u;
inline constexpr std::uint32_t kCr2StopMask = 3u << kCr2StopShift;

inline constexpr std::uint32_t kCr3Eie = 1u << 0;
inline constexpr std::uint32_t kCr3Hdsel = 1u << 3;
inline constexpr std::uint32_t kCr3Rtse = 1u << 8;
inline constexpr std::uint32_t kCr3Ctse = 1u << 9;
inline constexpr std::uint32_t kCr3Onebit = 1u << 11;

// USART2 on APB1. The firmware passes this to the MMIO adapter; the driver
// itself only sees the adapter, so the same code runs against a fake in tests.
inline constexpr std::uint32_t kUsart2Base = 0x40004400u;

struct Block {
    volatile std::uint32_t sr;
    volatile std::uint32_t dr;
    volatile std::uint32_t brr;
    volatile std::uint32_t cr1;
    volatile std::uint32_t cr2;
    volatile std::uint32_t cr3;
    volatile std::uint32_t gtpr;
};

static_assert(offsetof(Block, sr) == 0x00);
static_assert(offsetof(Block, dr) == 0x04);
static_assert(offsetof(Block, brr) == 0x08);
static_assert(offsetof(Block, cr1) == 0x0C);
static_assert(offsetof(Block, cr2) == 0x10);
static_assert(offsetof(Block, cr3) == 0x14);
static_assert(offsetof(Block, gtpr) == 0x18);
static_assert(sizeof(Block) == 0x1C);

}  // namespace uartcom::reg
