#!/usr/bin/env python3
"""Offline documentation renderer: actual sketch drawing functions, synthetic data.

Requires Python stdlib, a C++17 compiler and the installed M5GFX font headers.
No BLE, account/session reads, network access, device writes or image metadata.
"""

import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 800, 520  # 800x480 screen plus an explicit documentation caption.


def extract_block(source, signature, suffix=""):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1] + suffix
    raise ValueError(f"Unclosed source block: {signature}")


CANVAS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using std::min;
constexpr uint32_t TFT_BLACK = 0, TFT_WHITE = 255;
#define PROGMEM
struct GFXglyph {
    uint32_t bitmapOffset;
    uint8_t width, height, xAdvance;
    int8_t xOffset, yOffset;
};
struct GFXfont {
    uint8_t* bitmap;
    GFXglyph* glyph;
    uint16_t first, last;
    uint8_t yAdvance;
};
namespace classic {
#include "glcdfont.h"
}
namespace freefont {
#include "GFXFF/FreeSans24pt7b.h"
}
namespace fonts {
struct Tag { int kind; };
const Tag Font0{0}, FreeSans24pt7b{1};
}
enum textdatum_t {
    top_left, top_center, top_right, middle_left, middle_center,
    middle_right, bottom_left, bottom_center, bottom_right
};
namespace m5 {
struct rtc_datetime_t {
    struct { int year, month, date, weekDay; } date;
    struct { int hours, minutes, seconds; } time;
};
}
struct DeviceStub {
    struct PowerStub { int getBatteryLevel() { return 63; } } Power;
} M5;

class M5Canvas {
public:
    std::vector<uint8_t> pixels = std::vector<uint8_t>(800 * 520, 255);
    int width() const { return 800; }
    int height() const { return 480; }
    void fillSprite(uint32_t c) { std::fill(pixels.begin(), pixels.end(), c); }
    void fillRect(int x, int y, int w, int h, uint32_t c) {
        for (int py = std::max(0, y); py < std::min(520, y + h); ++py)
            for (int px = std::max(0, x); px < std::min(800, x + w); ++px)
                pixels[py * 800 + px] = static_cast<uint8_t>(c);
    }
    void drawPixel(int x, int y, uint32_t c) { fillRect(x, y, 1, 1, c); }
    void drawFastHLine(int x, int y, int w, uint32_t c) { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int x, int y, int h, uint32_t c) { fillRect(x, y, 1, h, c); }
    void drawRect(int x, int y, int w, int h, uint32_t c) {
        drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
    }
    void setTextColor(uint32_t c) { color = c; }
    void setTextDatum(textdatum_t d) { datum = d; }
    void setFont(const fonts::Tag* f) { font = f->kind; }
    void setTextSize(int size) { assert(size == 1); }
    void drawString(const char* value, int x, int y) {
        const std::string text(value);
        int textWidth = 0, above = 7, below = 1;
        if (font == 0) {
            textWidth = static_cast<int>(text.size()) * 6;
        } else {
            above = below = 0;
            const auto& f = freefont::FreeSans24pt7b;
            for (int i = 0; i <= f.last - f.first; ++i) {
                const auto& g = f.glyph[i];
                above = std::max(above, -static_cast<int>(g.yOffset));
                below = std::max(below, static_cast<int>(g.height) + g.yOffset);
            }
            for (unsigned char ch : text) {
                assert(ch >= f.first && ch <= f.last);
                textWidth += f.glyph[ch - f.first].xAdvance;
            }
        }
        const int textHeight = above + below;
        if (datum == top_right || datum == middle_right || datum == bottom_right) x -= textWidth;
        if (datum == top_center || datum == middle_center || datum == bottom_center) x -= textWidth / 2;
        if (datum == middle_left || datum == middle_center || datum == middle_right) y -= textHeight / 2;
        if (datum == bottom_left || datum == bottom_center || datum == bottom_right) y -= textHeight;
        for (unsigned char ch : text) {
            if (font == 0) {
                for (int col = 0; col < 5; ++col)
                    for (int row = 0; row < 8; ++row)
                        if (classic::font[ch * 5 + col] & (1 << row)) drawPixel(x + col, y + row, color);
                x += 6;
            } else {
                const auto& f = freefont::FreeSans24pt7b;
                const auto& g = f.glyph[ch - f.first];
                for (int row = 0; row < g.height; ++row)
                    for (int col = 0; col < g.width; ++col) {
                        const int bit = row * g.width + col;
                        if (f.bitmap[g.bitmapOffset + bit / 8] & (0x80 >> (bit % 8)))
                            drawPixel(x + g.xOffset + col, y + above + g.yOffset + row, color);
                    }
                x += g.xAdvance;
            }
        }
    }
private:
    uint32_t color = TFT_BLACK;
    textdatum_t datum = top_left;
    int font = 0;
};
'''

MAIN = r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    const bool synced = std::string(argv[1]) == "synced";
    hubStatus = synced ? HubStatus::Synced : HubStatus::Waiting;
    m5::rtc_datetime_t now{{2028, 2, 18, weekDayForDate(2028, 2, 18)}, {14, 32, 0}};
    if (synced) {
        codexActivity.valid = true;
        codexActivity.count = 112;
        codexActivity.endYear = 2028;
        codexActivity.endMonth = 2;
        codexActivity.endDay = 18;
        // Fictional fixtures: not read from a Hub, user sessions, or clock.
        const std::array<int, 2> sample[] = {
            {1,1}, {2,2}, {3,4}, {4,7}, {6,1}, {8,3}, {9,5},
            {10,8}, {13,2}, {14,4}, {16,1}, {17,3}, {18,7}
        };
        for (const auto& entry : sample) {
            const int index = 111 - (18 - entry[0]);
            const int turns = entry[1];
            codexActivity.turns[index] = turns;
            codexActivity.levels[index] = turns == 1 ? 1 : turns <= 3 ? 2 : turns <= 6 ? 3 : 4;
        }
        codexActivity.activeDays = 13;
        codexActivity.currentStreak = 3;
        codexActivity.longestStreak = 4;
    }
    drawCalendar(now);
    // Caption lies outside the 800x480 screen, so it cannot be mistaken for firmware UI.
    frame.drawFastHLine(0, 480, 800, TFT_BLACK);
    frame.setFont(&fonts::Font0);
    frame.setTextColor(TFT_BLACK);
    frame.setTextDatum(top_center);
    frame.drawString("SOFTWARE PREVIEW - FICTIONAL DATA", 400, 490);
    frame.drawString("NOT A DEVICE PHOTO", 400, 505);
    assert(frame.pixels.size() == 800 * 520);
    return std::fwrite(frame.pixels.data(), 1, frame.pixels.size(), stdout) == frame.pixels.size() ? 0 : 1;
}
'''


def renderer_source():
    source = (ROOT / "PaperMonoCalendar.ino").read_text()
    code = CANVAS
    for name in ("kActivityDayCount", "kTimePanelX", "kTimePanelY", "kTimePanelWidth", "kTimePanelHeight"):
        code += next(line for line in source.splitlines() if line.startswith("constexpr ") and f" {name} =" in line) + "\n"
    for signature in (
        "struct CodexActivity", "enum class HubStatus", "const char* const kWeekdays[]",
        "const char* const kWeekdayShort[]", "const char* const kMonthNames[]",
        "const char* const kMonthShort[]", "const uint8_t kPixelDigits[10][7]",
    ):
        code += extract_block(source, signature, ";\n")
    code += "M5Canvas frame; CodexActivity codexActivity; HubStatus hubStatus = HubStatus::Waiting;\n"
    for signature in (
        "const uint8_t* pixelGlyph(", "int pixelTextWidth(", "void drawPixelText(",
        "int clampBatteryLevel(", "bool isLeapYear(", "int daysInMonth(", "int weekDayForDate(",
        "const char* hubStatusText(", "void drawBattery(", "int32_t daysFromCivil(",
        "int activityIndexForDate(", "uint8_t activityLevelForDate(", "void drawActivityTile(",
        "int monthActiveDays(", "void drawCalendar(",
    ):
        code += extract_block(source, signature) + "\n"
    return code + MAIN


def png_chunk(name, payload):
    return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))


def png(pixels):
    if len(pixels) != WIDTH * HEIGHT or set(pixels) != {0, 255}:
        raise ValueError("Expected an 800x520 black/white raster")
    rows = b"".join(b"\0" + pixels[y * WIDTH:(y + 1) * WIDTH] for y in range(HEIGHT))
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 0, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(rows, 9))
            + png_chunk(b"IEND", b""))


def read_png(data):
    """Validate our metadata-free PNG format and return uncompressed pixels."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG signature")
    offset, types, compressed = 8, [], b""
    header = None
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        name = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if crc != zlib.crc32(name + payload):
            raise ValueError("Invalid PNG chunk checksum")
        types.append(name)
        if name == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        if name == b"IDAT":
            compressed += payload
        offset += 12 + size
    if types != [b"IHDR", b"IDAT", b"IEND"] or header != (WIDTH, HEIGHT, 8, 0, 0, 0, 0):
        raise ValueError("Unexpected PNG dimensions/format or metadata")
    rows = zlib.decompress(compressed)
    if len(rows) != HEIGHT * (WIDTH + 1) or any(rows[y * (WIDTH + 1)] for y in range(HEIGHT)):
        raise ValueError("Unexpected raster filter or size")
    return b"".join(rows[y * (WIDTH + 1) + 1:(y + 1) * (WIDTH + 1)] for y in range(HEIGHT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, required=True, help="Installed M5GFX/src/lgfx/Fonts directory")
    parser.add_argument("--check", action="store_true", help="Verify tracked previews match the renderer without writing files")
    args = parser.parse_args()
    for name in ("glcdfont.h", "GFXFF/FreeSans24pt7b.h"):
        if not (args.font_dir / name).is_file():
            parser.error(f"Missing font header: {name}")
    out = ROOT / "docs/images"
    with tempfile.TemporaryDirectory(prefix="papermono-preview-") as temp:
        cpp = Path(temp) / "render.cpp"
        exe = Path(temp) / "render"
        cpp.write_text(renderer_source())
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        # Keep host diagnostics visible without making compiler-specific warnings
        # fatal for extracted Arduino code. GCC 13 cannot infer the nonnegative
        # activity/streak bounds at snprintf; the fixtures are bounded above.
        # Compilation errors, runtime assertions and pixel mismatches remain fatal.
        subprocess.run(compiler + ["-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(args.font_dir.resolve()), str(cpp), "-o", str(exe)], check=True)
        for state in ("synced", "waiting"):
            pixels = subprocess.check_output([str(exe), state])
            image = png(pixels)
            destination = out / f"calendar-{state}.png"
            if args.check:
                if not destination.exists() or read_png(destination.read_bytes()) != pixels:
                    raise SystemExit(f"Preview out of date: {destination.name}")
            else:
                out.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(image)
            print(f"{'Checked' if args.check else 'Rendered'} {destination.name}: {WIDTH}x{HEIGHT}, {len(image)} bytes")


if __name__ == "__main__":
    main()
