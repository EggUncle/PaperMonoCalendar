"""Compile the sketch's actual pure decoder, without Arduino/BLE or user data."""

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def extract_block(source, signature, trailing=""):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1] + trailing
    raise ValueError(f"Missing closing brace for {signature}")


HARNESS = r'''
using Packet = std::vector<uint8_t>;

void put16(Packet& p, size_t offset, uint16_t value) {
    p[offset] = value & 255;
    p[offset + 1] = value >> 8;
}

void checksum(Packet& p) {
    put16(p, p.size() - 2, crc16Ccitt(p.data(), p.size() - 2));
}

Packet packet(uint8_t version, uint8_t count = 112) {
    const size_t header = version == 3 ? 23 : 16;
    Packet p(header + count + 2, 0);
    p[0] = 0xA7; p[1] = version; p[2] = count;
    p[3] = 1; p[4] = 1; p[5] = 1; p[6] = 1;
    put16(p, 8, 513); put16(p, 10, 1025);
    put16(p, 12, 2028); p[14] = 2; p[15] = 29;
    if (version == 3) {
        put16(p, 16, 2028);
        p[18] = 3; p[19] = 1; p[20] = 23; p[21] = 59; p[22] = 58;
    }
    if (count) p[header + count - 1] = (31 << 3) | 4;
    checksum(p);
    return p;
}

bool decode(const Packet& p, CodexActivity& value) {
    return decodeActivityPacket(p.data(), p.size(), value);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string test = argv[1];
    CodexActivity decoded;
    if (test == "crc") {
        const char* check = "123456789";
        assert(crc16Ccitt(reinterpret_cast<const uint8_t*>(check), 9) == 0x29B1);
    } else if (test == "versions") {
        for (uint8_t version : {2, 3}) {
            for (int count = 1; count <= 112; ++count) {
                const auto p = packet(version, count);
                assert(p.size() == static_cast<size_t>(count + (version == 2 ? 18 : 25)));
                assert(decode(p, decoded));
                assert(decoded.valid && decoded.count == count);
                assert(decoded.totalTurns == 513 && decoded.totalMinutes == 1025);
                assert(decoded.endYear == 2028 && decoded.endMonth == 2 && decoded.endDay == 29);
                assert(decoded.turns[count - 1] == 31 && decoded.levels[count - 1] == 4);
                assert(decoded.hasClock == (version == 3));
                if (version == 3) {
                    assert(decoded.clockYear == 2028 && decoded.clockMonth == 3 && decoded.clockDay == 1);
                    assert(decoded.clockHour == 23 && decoded.clockMinute == 59 && decoded.clockSecond == 58);
                }
            }
        }
    } else if (test == "truncated") {
        for (uint8_t version : {2, 3}) {
            const auto p = packet(version);
            for (size_t length = 0; length < p.size(); ++length) {
                assert(!decodeActivityPacket(p.data(), length, decoded));
            }
            auto extra = p; extra.push_back(0);
            assert(!decode(extra, decoded));
        }
    } else if (test == "corruption") {
        for (uint8_t version : {2, 3}) {
            const auto p = packet(version);
            for (size_t byte = 0; byte < p.size(); ++byte) {
                auto damaged = p; damaged[byte] ^= 1;
                assert(!decode(damaged, decoded));
            }
        }
    } else if (test == "invalid_fields") {
        for (uint8_t version : {2, 3}) {
            for (auto field : std::vector<std::pair<size_t, uint8_t>>{
                     {0, 0}, {1, 1}, {2, 0}, {2, 113}, {3, 7}, {14, 0}, {14, 13}, {15, 0}, {15, 30}}) {
                auto p = packet(version); p[field.first] = field.second; checksum(p);
                assert(!decode(p, decoded));
            }
            auto nonLeap = packet(version); put16(nonLeap, 12, 2027); checksum(nonLeap);
            assert(!decode(nonLeap, decoded));
            for (uint16_t year : {2023, 2100}) {
                auto p = packet(version); put16(p, 12, year); checksum(p);
                assert(!decode(p, decoded));
            }
        }
        for (auto field : std::vector<std::pair<size_t, uint8_t>>{
                 {18, 0}, {18, 13}, {19, 0}, {19, 32}, {20, 24}, {21, 60}, {22, 60}}) {
            auto p = packet(3); p[field.first] = field.second; checksum(p);
            assert(!decode(p, decoded));
        }
    } else if (test == "clock_equality") {
        CodexActivity other;
        auto p = packet(3); assert(decode(p, decoded));
        p[20] = 7; p[21] = 8; p[22] = 9; checksum(p);
        assert(decode(p, other));
        assert(activityEquals(decoded, other));
        p[23] = 9; checksum(p); assert(decode(p, other));
        assert(!activityEquals(decoded, other));
    } else {
        return 2;
    }
    return 0;
}
'''


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or not shutil.which(compiler[0]):
            raise RuntimeError("A C++17 compiler is required (set CXX if needed)")
        cls.temp = tempfile.TemporaryDirectory(prefix="papermono-protocol-")
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / "PaperMonoCalendar.ino").read_text()
        signatures = [
            ("struct CodexActivity", ";"),
            ("bool isLeapYear(", ""),
            ("int daysInMonth(", ""),
            ("bool isValidDateTime(", ""),
            ("uint16_t crc16Ccitt(", ""),
            ("bool activityEquals(", ""),
            ("bool decodeActivityPacket(", ""),
        ]
        code = """
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
constexpr size_t kActivityDayCount = 112;
struct SerialStub {
    void println(const char*) {}
    template<class... Args> void printf(const char*, Args...) {}
} Serial;
"""
        code += "\n".join(extract_block(source, signature, end) for signature, end in signatures)
        code += HARNESS
        cpp = Path(cls.temp.name) / "protocol.cpp"
        cpp.write_text(code)
        cls.binary = Path(cls.temp.name) / "protocol-test"
        subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(cls.binary)], check=True)

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True)

    def test_crc_reference(self):
        self.run_case("crc")

    def test_v2_v3_all_supported_day_counts(self):
        self.run_case("versions")

    def test_all_truncations_and_extra_byte(self):
        self.run_case("truncated")

    def test_each_byte_corruption(self):
        self.run_case("corruption")

    def test_invalid_version_count_weekday_and_date(self):
        self.run_case("invalid_fields")

    def test_clock_only_changes_do_not_dirty_activity(self):
        self.run_case("clock_equality")


class ScriptTests(unittest.TestCase):
    def test_documentation_relative_links(self):
        documents = [ROOT / "README.md", ROOT / "REFRESH_POLICY.md"]
        documents.extend((ROOT / "docs").glob("*.md"))
        for document in documents:
            for target in re.findall(r"\[[^\]]*\]\(([^\s)]+)\)", document.read_text()):
                if "://" in target or target.startswith("#"):
                    continue
                with self.subTest(document=document.name, target=target):
                    self.assertTrue((document.parent / target.split("#", 1)[0]).exists())

    def test_shell_syntax(self):
        for script in (ROOT / "tools").glob("*.sh"):
            with self.subTest(script=script.name):
                subprocess.run(["sh", "-n", str(script)], check=True)

    def test_missing_cli_fails_clearly(self):
        env = dict(os.environ, ARDUINO_CLI="papermono-deliberately-missing-cli")
        result = subprocess.run(["sh", str(ROOT / "tools/compile.sh")], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 127)
        self.assertIn("Arduino CLI not found", result.stderr)


if __name__ == "__main__":
    unittest.main()
