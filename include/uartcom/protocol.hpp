#pragma once

// Line commands plus a small binary frame, both sitting on top of the UART
// driver. The parser does not touch registers, so the host tests run it
// directly. See docs/protocol.md for the on-wire format.

#include "uartcom/crc32.hpp"
#include "uartcom/meta.hpp"
#include "uartcom/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace uartcom {

struct Identity {
    const char* board;
    const char* mcu;
    const char* usart;
};

struct BoardInfo {
    const char* source = "unset";
    std::uint32_t sysclk_hz = 0;
    std::uint32_t cpp_standard = 0;
    std::uint32_t constructors_ran = 0;
    std::uint32_t pll_enabled = 0;
    std::uint32_t data_ok = 0;
};

class Protocol {
   public:
    static constexpr std::size_t kMaxPayload = 256;
    static constexpr std::size_t kLineCap = 560;
    static constexpr std::size_t kOutCap = 1024;

    explicit Protocol(Identity identity) noexcept : identity_(identity) {}

    void set_link(const LinkStatus& link) noexcept { link_ = link; }
    void set_board(const BoardInfo& board) noexcept { board_ = board; }

    void ingest(std::uint8_t byte) {
        switch (state_) {
            case State::Line:
                on_line(byte);
                break;
            case State::Magic2:
                if (byte != 0x5A) {
                    reply("ERR sync\n");
                    state_ = State::Line;
                } else {
                    state_ = State::LenLo;
                }
                break;
            case State::LenLo:
                frame_len_ = byte;
                state_ = State::LenHi;
                break;
            case State::LenHi:
                frame_len_ = static_cast<std::uint16_t>(frame_len_ | (static_cast<std::uint16_t>(byte) << 8));
                filled_ = 0;
                if (frame_len_ > kMaxPayload) {
                    discard_left_ = static_cast<std::uint32_t>(frame_len_) + 4u;
                    state_ = State::Discard;
                    reply("ERR length\n");
                } else if (frame_len_ == 0) {
                    state_ = State::Crc0;
                } else {
                    state_ = State::Payload;
                }
                break;
            case State::Payload:
                payload_[filled_++] = byte;
                if (filled_ == frame_len_) {
                    state_ = State::Crc0;
                }
                break;
            case State::Crc0:
                crc_got_ = byte;
                state_ = State::Crc1;
                break;
            case State::Crc1:
                crc_got_ |= static_cast<std::uint32_t>(byte) << 8;
                state_ = State::Crc2;
                break;
            case State::Crc2:
                crc_got_ |= static_cast<std::uint32_t>(byte) << 16;
                state_ = State::Crc3;
                break;
            case State::Crc3: {
                crc_got_ |= static_cast<std::uint32_t>(byte) << 24;
                const auto expect = crc32(Span<const std::uint8_t>{payload_.data(), frame_len_});
                if (expect != crc_got_) {
                    reply("ERR crc\n");
                } else {
                    reply_frame();
                }
                state_ = State::Line;
                break;
            }
            case State::Discard:
                if (discard_left_ > 0) {
                    --discard_left_;
                }
                if (discard_left_ == 0) {
                    state_ = State::Line;
                }
                break;
        }
    }

    [[nodiscard]] std::size_t read_tx(Span<std::uint8_t> out) noexcept {
        std::size_t count = 0;
        while (count < out.size() && tx_count_ > 0) {
            out[count++] = tx_[tx_read_];
            tx_read_ = (tx_read_ + 1) % kOutCap;
            --tx_count_;
        }
        return count;
    }

    [[nodiscard]] std::size_t queued() const noexcept { return tx_count_; }

    [[nodiscard]] bool take_clear_request() noexcept {
        if (!clear_request_) {
            return false;
        }
        clear_request_ = false;
        return true;
    }

   private:
    enum class State : std::uint8_t {
        Line,
        Magic2,
        LenLo,
        LenHi,
        Payload,
        Crc0,
        Crc1,
        Crc2,
        Crc3,
        Discard
    };

    void on_line(std::uint8_t byte) {
        if (byte == '\r') {
            return;
        }
        if (byte == 0xA5 && line_len_ == 0 && !overflow_) {
            state_ = State::Magic2;
            return;
        }
        if (byte == '\n') {
            if (overflow_) {
                overflow_ = false;
                line_len_ = 0;
                reply("ERR line\n");
                return;
            }
            if (line_len_ == 0) {
                return;
            }
            line_[line_len_] = '\0';
            handle_line();
            line_len_ = 0;
            return;
        }
        if (overflow_) {
            return;
        }
        if (byte < 0x20 || byte > 0x7E) {
            reply("ERR bad_char\n");
            line_len_ = 0;
            overflow_ = false;
            return;
        }
        if (line_len_ + 1 >= kLineCap) {
            overflow_ = true;
            return;
        }
        line_[line_len_++] = static_cast<char>(byte);
    }

    void handle_line() {
        if (const char* rest = match_keyword("PING")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                reply("PONG\n");
            }
            return;
        }
        if (const char* rest = match_keyword("INFO")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                reply_info();
            }
            return;
        }
        if (const char* rest = match_keyword("ECHO")) {
            reply("ECHO");
            reply(rest);
            reply("\n");
            return;
        }
        if (const char* rest = match_keyword("HEX")) {
            handle_hex(skip_one_space(rest));
            return;
        }
        if (const char* rest = match_keyword("CRC32")) {
            handle_crc(skip_one_space(rest));
            return;
        }
        if (const char* rest = match_keyword("STAT")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                reply_stat();
            }
            return;
        }
        if (const char* rest = match_keyword("BAUD")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                reply_baud();
            }
            return;
        }
        if (const char* rest = match_keyword("RESET")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                clear_request_ = true;
                reply("OK\n");
            }
            return;
        }
        if (const char* rest = match_keyword("HELP")) {
            if (rest[0] != '\0') {
                reply("ERR args\n");
            } else {
                reply("HELP PING INFO ECHO HEX CRC32 STAT BAUD RESET\n");
            }
            return;
        }
        reply("ERR unknown\n");
    }

    void reply_info() {
        reply("INFO board=");
        reply(identity_.board);
        reply(" mcu=");
        reply(identity_.mcu);
        reply(" usart=");
        reply(identity_.usart);
        reply(" src=");
        reply(board_.source);
        reply(" pll=");
        append_u32(board_.pll_enabled);
        reply(" sysclk=");
        append_u32(board_.sysclk_hz);
        reply(" pclk=");
        append_u32(link_.kernel_clock_hz);
        reply(" baud=");
        append_u32(link_.baud_rate);
        reply(" actual=");
        append_u32(link_.actual_baud);
        reply(" brr=");
        append_u32(link_.brr);
        reply(" ppm=");
        append_i32(link_.error_ppm);
        reply(" cpp=");
        append_u32(board_.cpp_standard);
        reply(" ctors=");
        append_u32(board_.constructors_ran);
        reply(" data=");
        append_u32(board_.data_ok);
        reply("\n");
    }

    void reply_stat() {
        reply("STAT rx=");
        append_u32(link_.rx_bytes);
        reply(" tx=");
        append_u32(link_.tx_bytes);
        reply(" pe=");
        append_u32(link_.parity_errors);
        reply(" fe=");
        append_u32(link_.framing_errors);
        reply(" ne=");
        append_u32(link_.noise_errors);
        reply(" ore=");
        append_u32(link_.overrun_errors);
        reply(" swore=");
        append_u32(link_.software_overruns);
        reply(" rxq=");
        append_u32(link_.rx_queued);
        reply(" txq=");
        append_u32(link_.tx_queued);
        reply("\n");
    }

    void reply_baud() {
        reply("BAUD pclk=");
        append_u32(link_.kernel_clock_hz);
        reply(" baud=");
        append_u32(link_.baud_rate);
        reply(" actual=");
        append_u32(link_.actual_baud);
        reply(" brr=");
        append_u32(link_.brr);
        reply(" ppm=");
        append_i32(link_.error_ppm);
        reply("\n");
    }

    void handle_hex(const char* hex) {
        if (!decode_hex(hex)) {
            reply("ERR bad_hex\n");
            return;
        }
        reply("HEX");
        if (filled_ > 0) {
            reply(" ");
            for (std::uint16_t i = 0; i < filled_; ++i) {
                append_hex_byte(payload_[i]);
            }
        }
        reply("\n");
    }

    void handle_crc(const char* hex) {
        if (hex[0] != '\0' && !decode_hex(hex)) {
            reply("ERR bad_hex\n");
            return;
        }
        if (hex[0] == '\0') {
            filled_ = 0;
        }
        const auto value = crc32(Span<const std::uint8_t>{payload_.data(), filled_});
        reply("CRC32 ");
        append_hex32(value);
        reply("\n");
    }

    bool decode_hex(const char* hex) {
        filled_ = 0;
        if (hex[0] == '\0') {
            return true;
        }
        std::size_t length = 0;
        while (hex[length] != '\0') {
            ++length;
        }
        if ((length & 1u) != 0 || length / 2 > kMaxPayload) {
            return false;
        }
        std::uint16_t count = 0;
        for (std::size_t i = 0; i < length; i += 2) {
            const int hi = nibble(hex[i]);
            const int lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            payload_[count++] = static_cast<std::uint8_t>((hi << 4) | lo);
        }
        filled_ = count;
        return true;
    }

    void reply_frame() {
        if (!push_tx(0xA5) || !push_tx(0x5A)) {
            return;
        }
        (void)push_tx(static_cast<std::uint8_t>(frame_len_ & 0xFF));
        (void)push_tx(static_cast<std::uint8_t>((frame_len_ >> 8) & 0xFF));
        for (std::uint16_t i = 0; i < frame_len_; ++i) {
            (void)push_tx(payload_[i]);
        }
        const auto value = crc32(Span<const std::uint8_t>{payload_.data(), frame_len_});
        (void)push_tx(static_cast<std::uint8_t>(value & 0xFF));
        (void)push_tx(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        (void)push_tx(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        (void)push_tx(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }

    // Keyword match. The returned pointer is the remainder, which is either
    // empty or begins with the separating space. nullptr means "not this command".
    [[nodiscard]] const char* match_keyword(const char* keyword) const noexcept {
        const char* line = line_.data();
        while (*keyword != '\0') {
            if (*line++ != *keyword++) {
                return nullptr;
            }
        }
        if (*line == '\0' || *line == ' ') {
            return line;
        }
        return nullptr;
    }

    static const char* skip_one_space(const char* text) noexcept {
        if (text[0] == ' ') {
            return text + 1;
        }
        return text;
    }

    static int nibble(char character) noexcept {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    }

    void reply(const char* text) {
        std::size_t length = 0;
        while (text[length] != '\0') {
            ++length;
        }
        if (tx_count_ + length > kOutCap) {
            return;
        }
        for (std::size_t i = 0; i < length; ++i) {
            (void)push_tx(static_cast<std::uint8_t>(text[i]));
        }
    }

    void append_u32(std::uint32_t value) {
        char tmp[10];
        int count = 0;
        do {
            tmp[count++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value > 0);
        char text[10];
        int out = 0;
        while (count > 0) {
            text[out++] = tmp[--count];
        }
        text[out] = '\0';
        reply(text);
    }

    void append_i32(std::int32_t value) {
        if (value < 0) {
            reply("-");
            append_u32(static_cast<std::uint32_t>(-static_cast<std::int64_t>(value)));
            return;
        }
        append_u32(static_cast<std::uint32_t>(value));
    }

    void append_hex_byte(std::uint8_t value) {
        char text[3];
        text[0] = kHex[(value >> 4) & 0xF];
        text[1] = kHex[value & 0xF];
        text[2] = '\0';
        reply(text);
    }

    void append_hex32(std::uint32_t value) {
        char text[9];
        for (int shift = 28; shift >= 0; shift -= 4) {
            text[(28 - shift) / 4] = kHex[(value >> shift) & 0xF];
        }
        text[8] = '\0';
        reply(text);
    }

    bool push_tx(std::uint8_t byte) noexcept {
        if (tx_count_ >= kOutCap) {
            return false;
        }
        const std::size_t index = (tx_read_ + tx_count_) % kOutCap;
        tx_[index] = byte;
        ++tx_count_;
        return true;
    }

    static constexpr char kHex[] = "0123456789ABCDEF";

    Identity identity_;
    BoardInfo board_{};
    LinkStatus link_{};
    std::array<char, kLineCap> line_{};
    std::size_t line_len_ = 0;
    bool overflow_ = false;
    std::array<std::uint8_t, kMaxPayload> payload_{};
    std::uint16_t filled_ = 0;
    std::uint16_t frame_len_ = 0;
    std::uint32_t crc_got_ = 0;
    std::uint32_t discard_left_ = 0;
    State state_ = State::Line;
    std::array<std::uint8_t, kOutCap> tx_{};
    std::size_t tx_read_ = 0;
    std::size_t tx_count_ = 0;
    bool clear_request_ = false;
};

}  // namespace uartcom
