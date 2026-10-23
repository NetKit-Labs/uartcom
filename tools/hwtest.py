#!/usr/bin/env python3
"""Drive the NUCLEO-F446RE firmware from this computer.

The ST-Link virtual COM port is the USART2 link (115200 8N1). Opening the
port does not depend on pyserial; termios and select are enough.

UARTCOM_PORT overrides port discovery. The suite resets the MCU with OpenOCD
so the boot banner is part of the run.
"""

from __future__ import annotations

import glob
import os
import select
import subprocess
import sys
import termios
import time
import unittest
from pathlib import Path
from typing import Dict, List, Optional

ROOT = Path(__file__).resolve().parents[1]
BANNER = "UARTCOM STM32F446RE USART2 ready"
OPENOCD = os.environ.get("OPENOCD", "openocd")


def c_div(numerator: int, denominator: int) -> int:
    """C++ toward-zero integer division, including for a negative numerator."""
    sign = -1 if numerator < 0 else 1
    return sign * (abs(numerator) // abs(denominator))


def round_div(numerator: int, denominator: int) -> int:
    return (numerator + denominator // 2) // denominator


def brr_over16(clock: int, baud: int) -> int:
    return round_div(clock, baud)


def brr_over8(clock: int, baud: int) -> int:
    divisor = round_div(clock, baud)
    return ((divisor >> 3) << 4) | (divisor & 7)


def error_ppm(clock: int, baud: int, divisor: int) -> int:
    numerator = (clock - baud * divisor) * 1_000_000
    return c_div(numerator, baud * divisor)


def actual_baud(clock: int, divisor: int) -> int:
    return clock // divisor


def find_port() -> str:
    chosen = os.environ.get("UARTCOM_PORT")
    if chosen:
        return chosen
    matches = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not matches:
        raise RuntimeError("no ST-Link virtual COM port at /dev/cu.usbmodem* (set UARTCOM_PORT)")
    return matches[0]


class Port:
    def __init__(self, path: str) -> None:
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[3] = 0
        crtscts = getattr(termios, "CRTSCTS", 0)
        attrs[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | crtscts)
        attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self._pending = b""

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def write(self, data: bytes) -> None:
        view = data
        while view:
            count = os.write(self.fd, view)
            if count <= 0:
                raise OSError("short write on {}".format(self.path))
            view = view[count:]
        termios.tcdrain(self.fd)

    def _read_chunk(self, timeout: float) -> bytes:
        if self._pending:
            data = self._pending
            self._pending = b""
            return data
        ready, _, _ = select.select([self.fd], [], [], timeout)
        if not ready:
            return b""
        try:
            return os.read(self.fd, 4096)
        except BlockingIOError:
            return b""

    def read_exact(self, count: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        buffer = b""
        while len(buffer) < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            buffer += self._read_chunk(remaining)
        if len(buffer) < count:
            self._pending = buffer + self._pending
            raise TimeoutError("expected {} bytes, got {!r}".format(count, buffer))
        self._pending = buffer[count:] + self._pending
        return buffer[:count]

    def read_line(self, timeout: float) -> Optional[str]:
        deadline = time.monotonic() + timeout
        buffer = b""
        while b"\n" not in buffer and time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            chunk = self._read_chunk(min(0.2, remaining))
            if chunk:
                buffer += chunk
        if b"\n" not in buffer:
            self._pending = buffer + self._pending
            return None
        line, _, rest = buffer.partition(b"\n")
        self._pending = rest + self._pending
        return line.decode("ascii", "replace").rstrip("\r")

    def command(self, line: str, timeout: float = 2.0) -> str:
        self.write((line + "\n").encode("ascii"))
        reply = self.read_line(timeout)
        if reply is None:
            raise TimeoutError("no reply to {!r}".format(line))
        return reply

    def transact_frame(self, payload: bytes, crc: Optional[int] = None, timeout: float = 2.0) -> bytes:
        if crc is None:
            crc = bin_crc32(payload)
        frame = bytearray(b"\xA5\x5A")
        frame.append(len(payload) & 0xFF)
        frame.append((len(payload) >> 8) & 0xFF)
        frame.extend(payload)
        frame.extend(crc.to_bytes(4, "little"))
        self.write(bytes(frame))
        magic = self.read_exact(2, timeout)
        if magic == b"ER":
            rest = b""
            deadline = time.monotonic() + timeout
            while b"\n" not in rest and time.monotonic() < deadline:
                rest += self._read_chunk(0.2)
            text, sep, tail = (magic + rest).partition(b"\n")
            if sep:
                self._pending = tail + self._pending
            raise RuntimeError(text.decode("ascii", "replace"))
        if magic != b"\xA5\x5A":
            raise RuntimeError("bad frame magic {!r}".format(magic))
        length = int.from_bytes(self.read_exact(2, timeout), "little")
        body = self.read_exact(length + 4, timeout)
        data, crc_bytes = body[:length], body[length:]
        got = int.from_bytes(crc_bytes, "little")
        expect = bin_crc32(data)
        if got != expect:
            raise RuntimeError("crc {:08X} != {:08X}".format(got, expect))
        return data


def bin_crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = ((crc >> 1) ^ (0xEDB88320 & mask)) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def reset_board() -> None:
    cfg = ROOT / "firmware" / "openocd" / "nucleo_f446re.cfg"
    subprocess.run(
        [OPENOCD, "-f", "interface/stlink.cfg", "-f", str(cfg), "-c", "init", "-c", "reset run", "-c", "shutdown"],
        check=True,
        timeout=30,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def parse_fields(line: str, prefix: str) -> Dict[str, str]:
    if not line.startswith(prefix + " "):
        raise AssertionError("expected {} line, got {!r}".format(prefix, line))
    fields = {}
    for token in line.split()[1:]:
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


class BaudMathTests(unittest.TestCase):
    def test_golden_ppm_and_brr(self) -> None:
        self.assertEqual(brr_over16(45_000_000, 115200), 391)
        self.assertEqual(actual_baud(45_000_000, 391), 115089)
        self.assertEqual(error_ppm(45_000_000, 115200, 391), -959)
        self.assertEqual(brr_over16(16_000_000, 115200), 139)
        self.assertEqual(error_ppm(16_000_000, 115200, 139), -799)
        self.assertEqual(brr_over16(16_000_000, 9600), 1667)
        self.assertEqual(error_ppm(84_000_000, 9600, 8750), 0)
        self.assertEqual(brr_over8(45_000_000, 115200), 775)
        self.assertEqual(brr_over8(16_000_000, 115200), 275)
        self.assertEqual(bin_crc32(b"123456789"), 0xCBF43926)
        self.assertEqual(bin_crc32(bytes([0x00, 0xFF, 0x10])), 0x71D23404)
        self.assertEqual(bin_crc32(b""), 0)


class BoardTests(unittest.TestCase):
    port: Port
    banner_lines: List[str]

    @classmethod
    def setUpClass(cls) -> None:
        path = find_port()
        print("\nUART port", path)
        cls.port = Port(path)
        reset_board()
        deadline = time.monotonic() + 4.0
        cls.banner_lines = []
        while time.monotonic() < deadline:
            line = cls.port.read_line(0.3)
            if line is None:
                continue
            cls.banner_lines.append(line)
            if line == BANNER:
                return
        raise AssertionError("boot banner not seen, got {!r}".format(cls.banner_lines))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.port.close()

    def setUp(self) -> None:
        self.assertEqual(self.port.command("PING"), "PONG")

    def test_banner(self) -> None:
        self.assertIn(BANNER, self.banner_lines)

    def test_info_matches_baud_math(self) -> None:
        info = parse_fields(self.port.command("INFO"), "INFO")
        self.assertEqual(info["board"], "NUCLEO-F446RE")
        self.assertEqual(info["mcu"], "STM32F446RE")
        self.assertEqual(info["usart"], "USART2")
        self.assertEqual(info["pll"], "1")
        self.assertEqual(info["sysclk"], "180000000")
        self.assertEqual(info["pclk"], "45000000")
        self.assertIn(info["src"], ("hse_bypass", "hse_crystal", "hsi"))
        self.assertEqual(info["ctors"], "1")
        self.assertEqual(info["data"], "1")
        clock = int(info["pclk"])
        baud = int(info["baud"])
        self.assertEqual(int(info["brr"]), brr_over16(clock, baud))
        self.assertEqual(int(info["actual"]), actual_baud(clock, int(info["brr"])))
        self.assertEqual(int(info["ppm"]), error_ppm(clock, baud, int(info["brr"])))
        standard_path = ROOT / "build" / "firmware_cxx.txt"
        if standard_path.exists():
            self.assertEqual(info["cpp"], standard_path.read_text().strip())
        self.assertGreaterEqual(int(info["cpp"]), 201703)
        print("clock source", info["src"], "cpp", info["cpp"], "ppm", info["ppm"])

    def test_echo_cases(self) -> None:
        self.assertEqual(self.port.command("ECHO"), "ECHO")
        self.assertEqual(self.port.command("ECHO hello"), "ECHO hello")
        self.assertEqual(self.port.command("ECHO  hello"), "ECHO  hello")
        payload = "A" * 200
        self.assertEqual(self.port.command("ECHO " + payload), "ECHO " + payload)

    def test_hex_roundtrip_and_errors(self) -> None:
        self.assertEqual(self.port.command("HEX"), "HEX")
        self.assertEqual(self.port.command("HEX 00ff10"), "HEX 00FF10")
        pattern = bytes(i & 0xFF for i in range(128))
        encoded = pattern.hex()
        self.assertEqual(self.port.command("HEX " + encoded), "HEX " + encoded.upper())
        self.assertEqual(self.port.command("HEX 0"), "ERR bad_hex")
        self.assertEqual(self.port.command("HEX zz"), "ERR bad_hex")

    def test_crc32_known_vector(self) -> None:
        self.assertEqual(self.port.command("CRC32"), "CRC32 00000000")
        self.assertEqual(self.port.command("CRC32 313233343536373839"), "CRC32 CBF43926")
        self.assertEqual(self.port.command("CRC32 00ff10"), "CRC32 71D23404")

    def test_binary_frame_and_bad_crc(self) -> None:
        payload = bytes([0x00, 0xFF, 0x10, 0x7E, 0x20]) + bytes(range(64))
        self.assertEqual(self.port.transact_frame(payload), payload)
        try:
            self.port.transact_frame(b"\x01\x02", crc=0)
        except RuntimeError as exc:
            self.assertEqual(str(exc), "ERR crc")
        else:
            self.fail("bad crc was accepted")
        self.assertEqual(self.port.command("PING"), "PONG")

    def test_counters_move_and_reset(self) -> None:
        before = parse_fields(self.port.command("STAT"), "STAT")
        self.port.command("ECHO xyz")
        after = parse_fields(self.port.command("STAT"), "STAT")
        self.assertGreater(int(after["rx"]), int(before["rx"]))
        self.assertGreater(int(after["tx"]), int(before["tx"]))
        self.assertEqual(self.port.command("RESET"), "OK")
        cleared = parse_fields(self.port.command("STAT"), "STAT")
        self.assertLess(int(cleared["rx"]), int(after["rx"]))

    def test_unknown_help_and_burst(self) -> None:
        self.assertEqual(self.port.command("NOPE"), "ERR unknown")
        self.assertTrue(self.port.command("HELP").startswith("HELP "))
        self.assertEqual(self.port.command("PING extra"), "ERR args")
        for i in range(30):
            self.assertEqual(self.port.command("ECHO " + str(i)), "ECHO " + str(i))

    def test_baud_command(self) -> None:
        baud = parse_fields(self.port.command("BAUD"), "BAUD")
        clock = int(baud["pclk"])
        rate = int(baud["baud"])
        divisor = int(baud["brr"])
        self.assertEqual(divisor, brr_over16(clock, rate))
        self.assertEqual(int(baud["ppm"]), error_ppm(clock, rate, divisor))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
