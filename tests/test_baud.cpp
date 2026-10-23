#include "test_harness.hpp"

#include "uartcom/baud.hpp"

namespace {

UARTCOM_TEST(baud_golden_table) {
    struct Row {
        std::uint32_t clock;
        std::uint32_t baud;
        uartcom::Oversampling over;
        std::uint32_t brr;
        std::uint32_t actual;
        std::int32_t ppm;
    };
    const Row rows[] = {
        {45000000, 115200, uartcom::Oversampling::By16, 391, 115089, -959},
        {16000000, 115200, uartcom::Oversampling::By16, 139, 115107, -799},
        {16000000, 9600, uartcom::Oversampling::By16, 1667, 9598, -199},
        {84000000, 9600, uartcom::Oversampling::By16, 8750, 9600, 0},
        {45000000, 9600, uartcom::Oversampling::By16, 4688, 9598, -106},
        {45000000, 115200, uartcom::Oversampling::By8, 775, 115089, -959},
        {16000000, 115200, uartcom::Oversampling::By8, 275, 115107, -799},
    };
    for (const auto& row : rows) {
        const auto result = uartcom::compute_baud(row.clock, row.baud, row.over);
        CHECK(result.ok());
        CHECK_EQ(result.program.brr, row.brr);
        CHECK_EQ(result.program.actual_baud, row.actual);
        CHECK_EQ(result.program.error_ppm, row.ppm);
        if (row.over == uartcom::Oversampling::By8) {
            CHECK_EQ(result.program.brr & 0x8u, 0u);
        }
    }
}

UARTCOM_TEST(baud_rejects_illegal_inputs) {
    CHECK_EQ(uartcom::compute_baud(0, 115200, uartcom::Oversampling::By16).error, uartcom::BaudError::ZeroClock);
    CHECK_EQ(uartcom::compute_baud(16000000, 0, uartcom::Oversampling::By16).error, uartcom::BaudError::ZeroBaud);
    CHECK_EQ(uartcom::compute_baud(16000000, 3000000, uartcom::Oversampling::By16).error,
             uartcom::BaudError::BaudTooHigh);
    CHECK_EQ(uartcom::compute_baud(16000000, 10, uartcom::Oversampling::By16).error,
             uartcom::BaudError::DivisorTooLarge);
}

}  // namespace
