// GCC rewrites byte-fill loops into memset/memcpy/strlen calls. These
// functions are the implementations of those calls, so that rewrite would
// recurse until the stack is gone.
#pragma GCC optimize("no-tree-loop-distribute-patterns")

#include <cstddef>
#include <cstdint>

extern "C" char _estack[];
extern "C" char _sidata[];
extern "C" char _sdata[];
extern "C" char _edata[];
extern "C" char _sbss[];
extern "C" char _ebss[];
extern "C" void (*__init_array_start[])();
extern "C" void (*__init_array_end[])();

extern "C" int firmware_main();
extern "C" void SystemInit();
extern "C" void USART2_IRQHandler();
extern "C" void Reset_Handler();

extern "C" void fault_blink() {
    volatile std::uint32_t* const ahb = reinterpret_cast<volatile std::uint32_t*>(0x40023830u);
    *ahb |= 1u;
    volatile std::uint32_t* const gpio = reinterpret_cast<volatile std::uint32_t*>(0x40020000u);
    gpio[0] = (gpio[0] & ~(3u << 10)) | (1u << 10);
    while (true) {
        gpio[6] = 1u << 5;
        for (std::uint32_t i = 0; i < 200000u; ++i) {
            asm volatile("" : : "r"(i) : "memory");
        }
        gpio[6] = 1u << 21;
        for (std::uint32_t i = 0; i < 200000u; ++i) {
            asm volatile("" : : "r"(i) : "memory");
        }
    }
}

extern "C" void Default_Handler() { fault_blink(); }
extern "C" void NMI_Handler() { fault_blink(); }
extern "C" void HardFault_Handler() { fault_blink(); }
extern "C" void MemManage_Handler() { fault_blink(); }
extern "C" void BusFault_Handler() { fault_blink(); }
extern "C" void UsageFault_Handler() { fault_blink(); }

extern "C" void* memset(void* dst, int value, std::size_t count) {
    auto* bytes = static_cast<volatile unsigned char*>(dst);
    const auto fill = static_cast<unsigned char>(value);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = fill;
    }
    return dst;
}

extern "C" std::size_t strlen(const char* text) {
    std::size_t count = 0;
    const auto* bytes = reinterpret_cast<const volatile unsigned char*>(text);
    while (bytes[count] != 0) {
        ++count;
    }
    return count;
}

extern "C" void* memcpy(void* dst, const void* src, std::size_t count) {
    auto* out = static_cast<unsigned char*>(dst);
    const auto* in = static_cast<const unsigned char*>(src);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
    return dst;
}

extern "C" void* memmove(void* dst, const void* src, std::size_t count) {
    auto* out = static_cast<unsigned char*>(dst);
    const auto* in = static_cast<const unsigned char*>(src);
    if (out == in || count == 0) {
        return dst;
    }
    if (out < in) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (std::size_t i = count; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }
    return dst;
}

extern "C" void* __dso_handle;
void* __dso_handle = nullptr;
extern "C" int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }

static void run_init_array() {
    for (auto* fn = __init_array_start; fn != __init_array_end; ++fn) {
        (*fn)();
    }
}

#define DH reinterpret_cast<const void*>(&Default_Handler)
#define DH10 DH, DH, DH, DH, DH, DH, DH, DH, DH, DH

// 16 system entries + 97 external IRQs (0..96). USART2 is IRQ 38, table index 54.
[[gnu::section(".isr_vector"), gnu::used]] const void* const g_vectors[] = {
    _estack,
    reinterpret_cast<const void*>(&Reset_Handler),
    reinterpret_cast<const void*>(&NMI_Handler),
    reinterpret_cast<const void*>(&HardFault_Handler),
    reinterpret_cast<const void*>(&MemManage_Handler),
    reinterpret_cast<const void*>(&BusFault_Handler),
    reinterpret_cast<const void*>(&UsageFault_Handler),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    DH,
    DH,
    nullptr,
    DH,
    DH,
    DH10,
    DH10,
    DH10,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
    reinterpret_cast<const void*>(&USART2_IRQHandler),
    DH10,
    DH10,
    DH10,
    DH10,
    DH10,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
    DH,
};

static_assert(sizeof(g_vectors) / sizeof(g_vectors[0]) == 113);

extern "C" void Reset_Handler() {
    auto* src = reinterpret_cast<std::uint32_t*>(_sidata);
    auto* dst = reinterpret_cast<std::uint32_t*>(_sdata);
    auto* end = reinterpret_cast<std::uint32_t*>(_edata);
    while (dst < end) {
        *dst++ = *src++;
    }

    auto* bss = reinterpret_cast<std::uint32_t*>(_sbss);
    auto* bss_end = reinterpret_cast<std::uint32_t*>(_ebss);
    while (bss < bss_end) {
        *bss++ = 0;
    }

    SystemInit();
    run_init_array();
    asm volatile("" : : "r"(g_vectors) : "memory");
    firmware_main();
    while (true) {
    }
}
