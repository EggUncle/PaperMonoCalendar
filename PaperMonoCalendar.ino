#include <Arduino.h>
#include <BLEDevice.h>
#include <M5Unified.h>

namespace {

constexpr uint32_t kRtcPollIntervalMs = 1000;
constexpr uint16_t kQualityRefreshInterval = 10;
constexpr uint8_t kFrontlightBrightness = 0;
constexpr uint32_t kActivitySyncIntervalMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kActivityRetryIntervalMs = 60UL * 1000UL;
constexpr size_t kActivityDayCount = 112;
constexpr int kTimePanelX = 584;
constexpr int kTimePanelY = 114;
constexpr int kTimePanelWidth = 192;
constexpr int kTimePanelHeight = 118;
constexpr int kHubStatusPanelX = 460;
constexpr int kHubStatusPanelY = 19;
constexpr int kHubStatusPanelWidth = 220;
constexpr int kHubStatusPanelHeight = 14;
constexpr char kHubServiceUuid[] = "E7D6D101-5A2D-4B7A-9C0E-123456789ABC";
constexpr char kActivityCharacteristicUuid[] = "E7D6D10A-5A2D-4B7A-9C0E-123456789ABC";

struct CodexActivity {
    bool valid = false;
    uint8_t count = 0;
    uint8_t startWeekday = 0;
    uint8_t activeDays = 0;
    uint8_t currentStreak = 0;
    uint8_t longestStreak = 0;
    uint16_t totalTurns = 0;
    uint16_t totalMinutes = 0;
    uint16_t endYear = 0;
    uint8_t endMonth = 0;
    uint8_t endDay = 0;
    bool hasClock = false;
    uint16_t clockYear = 0;
    uint8_t clockMonth = 0;
    uint8_t clockDay = 0;
    uint8_t clockHour = 0;
    uint8_t clockMinute = 0;
    uint8_t clockSecond = 0;
    uint8_t levels[kActivityDayCount] = {};
    uint8_t turns[kActivityDayCount] = {};
};

enum class HubStatus : uint8_t {
    Waiting,
    Scanning,
    NotFound,
    LinkFailed,
    AuthFailed,
    NoService,
    NoData,
    BadData,
    Synced,
};

M5Canvas frame(&M5.Display);
M5Canvas timeFrame(&M5.Display);
M5Canvas hubStatusFrame(&M5.Display);

uint32_t lastRtcPollMs = 0;
uint32_t lastMinuteKey = UINT32_MAX;
uint32_t lastDateKey = UINT32_MAX;
uint16_t textRefreshesSinceQuality = 0;
bool forceQualityRefresh = true;
bool calendarDirty = false;
uint32_t nextActivitySyncMs = 2500;
CodexActivity codexActivity;
BLESecurityCallbacks securityCallbacks;
HubStatus hubStatus = HubStatus::Waiting;

const char* const kWeekdays[] = {
    "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
    "THURSDAY", "FRIDAY", "SATURDAY",
};

const char* const kWeekdayShort[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

const char* const kMonthNames[] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};

const char* const kMonthShort[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

// Exact 5x7 numeral shapes approved in the HTML prototype. Each byte is one
// five-pixel row, most-significant visible bit first.
const uint8_t kPixelDigits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
};

const uint8_t* pixelGlyph(char character) {
    static const uint8_t colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
    static const uint8_t percent[7] = {0x19, 0x1A, 0x04, 0x04, 0x08, 0x16, 0x06};
    static const uint8_t blank[7] = {};
    if (character >= '0' && character <= '9') return kPixelDigits[character - '0'];
    if (character == ':') return colon;
    if (character == '/') return slash;
    if (character == '%') return percent;
    return blank;
}

int pixelTextWidth(const char* text, int scale) {
    const int length = strlen(text);
    return length == 0 ? 0 : length * 6 * scale - scale;
}

void drawPixelText(M5Canvas& canvas, const char* text, int x, int y,
                   int scale, uint32_t color, textdatum_t datum = top_left) {
    const int width = pixelTextWidth(text, scale);
    const int height = 7 * scale;
    if (datum == top_right || datum == middle_right) x -= width;
    if (datum == top_center || datum == middle_center) x -= width / 2;
    if (datum == middle_left || datum == middle_center || datum == middle_right) {
        y -= height / 2;
    }
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const uint8_t* rows = pixelGlyph(*cursor);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (rows[row] & (1U << (4 - column))) {
                    canvas.fillRect(x + column * scale, y + row * scale,
                                    scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

int clampBatteryLevel(int level) {
    if (level < 0) return -1;
    if (level > 100) return 100;
    return level;
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
    static constexpr uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month == 2 && isLeapYear(year)) return 29;
    return (month >= 1 && month <= 12) ? days[month - 1] : 0;
}

bool isValidDateTime(int year, int month, int day,
                     int hour, int minute, int second) {
    return year >= 2024 && year <= 2099 && month >= 1 && month <= 12 &&
           day >= 1 && day <= daysInMonth(year, month) &&
           hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}

int weekDayForDate(int year, int month, int day) {
    static constexpr int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 +
            offsets[month - 1] + day) % 7;
}

int monthFromBuildDate(const char* month) {
    static constexpr const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; ++i) {
        if (strncmp(month, months[i], 3) == 0) return i + 1;
    }
    return 1;
}

void setRtcFromBuildTime() {
    char monthName[4] = {};
    int day = 1;
    int year = 2026;
    int hour = 0;
    int minute = 0;
    int second = 0;
    sscanf(__DATE__, "%3s %d %d", monthName, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    const int month = monthFromBuildDate(monthName);
    const int weekDay = weekDayForDate(year, month, day);
    M5.Rtc.setDateTime({{year, month, day, weekDay},
                        {hour, minute, second}});
    Serial.printf("RTC initialized from build time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
}

bool rtcDateTimeIsValid(const m5::rtc_datetime_t& dt) {
    return isValidDateTime(dt.date.year, dt.date.month, dt.date.date,
                           dt.time.hours, dt.time.minutes, dt.time.seconds) &&
           dt.date.weekDay >= 0 && dt.date.weekDay <= 6;
}

uint32_t minuteKey(const m5::rtc_datetime_t& dt) {
    return (((static_cast<uint32_t>(dt.date.year) * 13U + dt.date.month) * 32U +
             dt.date.date) * 24U + dt.time.hours) * 60U + dt.time.minutes;
}

uint32_t dateKey(const m5::rtc_datetime_t& dt) {
    return static_cast<uint32_t>(dt.date.year) * 10000U +
           static_cast<uint32_t>(dt.date.month) * 100U + dt.date.date;
}

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool activityEquals(const CodexActivity& lhs, const CodexActivity& rhs) {
    return lhs.valid == rhs.valid && lhs.count == rhs.count &&
           lhs.startWeekday == rhs.startWeekday &&
           lhs.activeDays == rhs.activeDays &&
           lhs.currentStreak == rhs.currentStreak &&
           lhs.longestStreak == rhs.longestStreak &&
           lhs.totalTurns == rhs.totalTurns &&
           lhs.totalMinutes == rhs.totalMinutes &&
           lhs.endYear == rhs.endYear && lhs.endMonth == rhs.endMonth &&
           lhs.endDay == rhs.endDay &&
           memcmp(lhs.levels, rhs.levels, kActivityDayCount) == 0 &&
           memcmp(lhs.turns, rhs.turns, kActivityDayCount) == 0;
}

const char* hubStatusText(HubStatus status) {
    switch (status) {
        case HubStatus::Scanning: return "HUB / SCANNING";
        case HubStatus::NotFound: return "HUB / NOT FOUND";
        case HubStatus::LinkFailed: return "HUB / LINK FAIL";
        case HubStatus::AuthFailed: return "HUB / AUTH FAIL";
        case HubStatus::NoService: return "HUB / NO SERVICE";
        case HubStatus::NoData: return "HUB / NO DATA";
        case HubStatus::BadData: return "HUB / BAD DATA";
        case HubStatus::Synced: return "HUB / SYNCED";
        case HubStatus::Waiting:
        default: return "HUB / WAITING";
    }
}

bool decodeActivityPacket(const uint8_t* bytes, size_t length,
                          CodexActivity& decoded) {
    constexpr size_t v2HeaderLength = 16;
    constexpr size_t v3HeaderLength = 23;
    constexpr size_t crcLength = 2;
    if (length < v2HeaderLength + crcLength || bytes[0] != 0xA7 ||
        (bytes[1] != 2 && bytes[1] != 3)) {
        Serial.println("BLE activity: invalid header");
        return false;
    }
    const bool hasClock = bytes[1] == 3;
    const size_t headerLength = hasClock ? v3HeaderLength : v2HeaderLength;
    const uint8_t count = bytes[2];
    const size_t expectedLength = headerLength + count + crcLength;
    if (count == 0 || count > kActivityDayCount || length != expectedLength ||
        bytes[3] > 6 || !isValidDateTime(
            static_cast<uint16_t>(bytes[12]) |
                static_cast<uint16_t>(bytes[13]) << 8,
            bytes[14], bytes[15], 0, 0, 0)) {
        Serial.printf("BLE activity: invalid size/count (%u bytes, %u days)\n",
                      static_cast<unsigned>(length), count);
        return false;
    }
    if (hasClock && !isValidDateTime(
            static_cast<uint16_t>(bytes[16]) |
                static_cast<uint16_t>(bytes[17]) << 8,
            bytes[18], bytes[19], bytes[20], bytes[21], bytes[22])) {
        Serial.println("BLE activity: invalid Hub clock");
        return false;
    }
    const uint16_t receivedCrc = static_cast<uint16_t>(bytes[length - 2]) |
                                 static_cast<uint16_t>(bytes[length - 1]) << 8;
    const uint16_t calculatedCrc = crc16Ccitt(bytes, length - crcLength);
    if (receivedCrc != calculatedCrc) {
        Serial.printf("BLE activity: CRC mismatch (%04X != %04X)\n",
                      receivedCrc, calculatedCrc);
        return false;
    }

    decoded = CodexActivity{};
    decoded.valid = true;
    decoded.count = count;
    decoded.startWeekday = bytes[3];
    decoded.activeDays = bytes[4];
    decoded.currentStreak = bytes[5];
    decoded.longestStreak = bytes[6];
    decoded.totalTurns = static_cast<uint16_t>(bytes[8]) |
                         static_cast<uint16_t>(bytes[9]) << 8;
    decoded.totalMinutes = static_cast<uint16_t>(bytes[10]) |
                           static_cast<uint16_t>(bytes[11]) << 8;
    decoded.endYear = static_cast<uint16_t>(bytes[12]) |
                      static_cast<uint16_t>(bytes[13]) << 8;
    decoded.endMonth = bytes[14];
    decoded.endDay = bytes[15];
    decoded.hasClock = hasClock;
    if (hasClock) {
        decoded.clockYear = static_cast<uint16_t>(bytes[16]) |
                            static_cast<uint16_t>(bytes[17]) << 8;
        decoded.clockMonth = bytes[18];
        decoded.clockDay = bytes[19];
        decoded.clockHour = bytes[20];
        decoded.clockMinute = bytes[21];
        decoded.clockSecond = bytes[22];
    }
    for (uint8_t i = 0; i < count; ++i) {
        decoded.levels[i] = bytes[headerLength + i] & 0x07;
        decoded.turns[i] = bytes[headerLength + i] >> 3;
    }
    return true;
}

bool syncActivityViaBLE(bool& changed, bool& statusChanged) {
    changed = false;
    statusChanged = false;
    HubStatus nextStatus = HubStatus::NotFound;
    Serial.println("BLE activity: scanning for M5StackHub...");
    BLEDevice::init("PaperMonoCalendar");
    BLEDevice::setSecurityCallbacks(&securityCallbacks);
    BLESecurity security;
    security.setAuthenticationMode(true, false, true);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey();
    security.setRespEncryptionKey();
    security.setForceAuthentication(true);
    BLEScan* scan = BLEDevice::getScan();
    // macOS may place the local name or 128-bit service UUID in the scan
    // response rather than the first advertisement packet.
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(30);
    BLEScanResults* results = scan->start(5, false);

    BLEAdvertisedDevice target;
    bool found = false;
    const BLEUUID serviceUuid(kHubServiceUuid);
    if (results != nullptr) {
        for (int i = 0; i < results->getCount(); ++i) {
            BLEAdvertisedDevice candidate = results->getDevice(i);
            const bool serviceMatch = candidate.haveServiceUUID() &&
                                      candidate.isAdvertisingService(serviceUuid);
            const bool nameMatch = candidate.haveName() &&
                                   candidate.getName() == "StackChanCodex";
            if (serviceMatch || nameMatch) {
                target = candidate;
                found = true;
                break;
            }
        }
    }

    bool success = false;
    if (!found) {
        Serial.println("BLE activity: Hub advertisement not found");
    } else {
        nextStatus = HubStatus::LinkFailed;
        BLEClient* client = BLEDevice::createClient();
        if (client != nullptr && client->connect(&target)) {
            client->setMTU(185);
            const bool authenticated = client->secureConnection();
            if (!authenticated) {
                Serial.println("BLE activity: secure connection failed");
                nextStatus = HubStatus::AuthFailed;
            }
            BLERemoteService* service = authenticated
                ? client->getService(serviceUuid) : nullptr;
            if (authenticated && service == nullptr) {
                nextStatus = HubStatus::NoService;
            }
            BLERemoteCharacteristic* characteristic =
                service == nullptr ? nullptr :
                service->getCharacteristic(BLEUUID(kActivityCharacteristicUuid));
            if (characteristic != nullptr && characteristic->canRead()) {
                nextStatus = HubStatus::BadData;
                const String raw = characteristic->readValue();
                CodexActivity decoded;
                if (decodeActivityPacket(
                        reinterpret_cast<const uint8_t*>(raw.c_str()),
                        raw.length(), decoded)) {
                    changed = !activityEquals(codexActivity, decoded);
                    if (decoded.hasClock) {
                        const auto before = M5.Rtc.getDateTime();
                        const bool minuteChanged =
                            before.date.year != decoded.clockYear ||
                            before.date.month != decoded.clockMonth ||
                            before.date.date != decoded.clockDay ||
                            before.time.hours != decoded.clockHour ||
                            before.time.minutes != decoded.clockMinute;
                        const int weekDay = weekDayForDate(
                            decoded.clockYear, decoded.clockMonth,
                            decoded.clockDay);
                        M5.Rtc.setDateTime({
                            {decoded.clockYear, decoded.clockMonth,
                             decoded.clockDay, weekDay},
                            {decoded.clockHour, decoded.clockMinute,
                             decoded.clockSecond},
                        });
                        if (minuteChanged) lastMinuteKey = UINT32_MAX;
                        Serial.printf(
                            "RTC synced from Hub: %04u-%02u-%02u %02u:%02u:%02u\n",
                            decoded.clockYear, decoded.clockMonth,
                            decoded.clockDay, decoded.clockHour,
                            decoded.clockMinute, decoded.clockSecond);
                    }
                    codexActivity = decoded;
                    success = true;
                    nextStatus = HubStatus::Synced;
                    Serial.printf("BLE activity: %u days, %u turns, streak %u%s\n",
                                  decoded.count, decoded.totalTurns,
                                  decoded.currentStreak,
                                  changed ? " (updated)" : "");
                }
            } else {
                Serial.println("BLE activity: read characteristic unavailable");
                if (service != nullptr) nextStatus = HubStatus::NoData;
            }
            client->disconnect();
        } else {
            Serial.println("BLE activity: Hub connection failed");
        }
    }

    statusChanged = nextStatus != hubStatus;
    hubStatus = nextStatus;
    scan->clearResults();
    // Keep controller memory reserved so the hourly sync (and one-minute
    // failure retry) can initialize BLE again. deinit(true) permanently
    // releases that memory in the ESP32 Arduino BLE implementation.
    BLEDevice::deinit(false);
    return success;
}

void drawBattery(int screenWidth) {
    const int level = clampBatteryLevel(M5.Power.getBatteryLevel());
    constexpr int iconWidth = 36;
    constexpr int iconHeight = 11;
    const int iconX = screenWidth - 96;
    constexpr int iconY = 20;

    frame.drawRect(iconX, iconY, iconWidth, iconHeight, TFT_BLACK);
    frame.fillRect(iconX + iconWidth, iconY + 3, 3, 5, TFT_BLACK);
    if (level >= 0) {
        const int fillWidth = (iconWidth - 6) * level / 100;
        if (fillWidth > 0) {
            frame.fillRect(iconX + 3, iconY + 3, fillWidth, iconHeight - 6, TFT_BLACK);
        }
    }

    char batteryText[8];
    if (level >= 0) {
        snprintf(batteryText, sizeof(batteryText), "%d%%", level);
    } else {
        snprintf(batteryText, sizeof(batteryText), "--");
    }
    drawPixelText(frame, batteryText, screenWidth - 24, iconY + iconHeight / 2,
                  1, TFT_BLACK, middle_right);
}

int32_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear =
        (153 * adjustedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
                              yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<int32_t>(dayOfEra);
}

int activityIndexForDate(int year, int month, int day) {
    if (!codexActivity.valid || codexActivity.count == 0) return -1;
    const int32_t delta =
        daysFromCivil(codexActivity.endYear, codexActivity.endMonth,
                      codexActivity.endDay) -
        daysFromCivil(year, month, day);
    const int index = static_cast<int>(codexActivity.count) - 1 - delta;
    return index >= 0 && index < codexActivity.count ? index : -1;
}

uint8_t activityLevelForDate(int year, int month, int day) {
    const int index = activityIndexForDate(year, month, day);
    return index < 0 ? 0 : min<uint8_t>(4, codexActivity.levels[index]);
}

void drawActivityTile(int x, int y, int width, int height, uint8_t level) {
    if (level == 0) return;
    // Ordered 4x4 Bayer coverage gives four calm gray impressions without a
    // four-gray panel update, preserving the official monochrome OTP baseline.
    static constexpr uint8_t bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6},
        {3, 11, 1, 9}, {15, 7, 13, 5},
    };
    const uint8_t thresholds[] = {0, 3, 6, 9, 12};
    const uint8_t threshold = thresholds[min<uint8_t>(level, 4)];
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            if (bayer[py & 3][px & 3] < threshold) {
                frame.drawPixel(px, py, TFT_BLACK);
            }
        }
    }
}

void drawActivityLegend(int centerX, int y) {
    constexpr int size = 16;
    constexpr int gap = 8;
    constexpr int totalWidth = size * 5 + gap * 4;
    const int startX = centerX - totalWidth / 2;
    for (int level = 0; level <= 4; ++level) {
        const int x = startX + level * (size + gap);
        frame.drawRoundRect(x, y, size, size, 4, TFT_BLACK);
        if (level > 0) drawActivityTile(x, y, size, size, level);
    }
}

int monthActiveDays(int year, int month) {
    int activeDays = 0;
    for (int day = 1; day <= daysInMonth(year, month); ++day) {
        const int index = activityIndexForDate(year, month, day);
        if (index < 0) continue;
        if (codexActivity.turns[index] > 0) ++activeDays;
    }
    return activeDays;
}

void drawCalendar(const m5::rtc_datetime_t& dt) {
    const int width = frame.width();
    constexpr int leftX = 24;
    constexpr int leftWidth = 516;
    constexpr int rightBorderX = 568;
    constexpr int rightX = 590;
    constexpr int rightEdge = 776;
    constexpr int monthTop = 58;
    constexpr int monthBottom = 130;
    constexpr int weekdaysBottom = 154;
    constexpr int datesBottom = 462;

    frame.fillSprite(TFT_WHITE);
    frame.setTextColor(TFT_BLACK);
    frame.setTextDatum(top_left);
    frame.setFont(&fonts::Font0);
    frame.setTextSize(1);
    frame.drawString("C A L E N D A R", leftX, 19);
    frame.setFont(&fonts::Font0);
    char todayHeader[32];
    snprintf(todayHeader, sizeof(todayHeader), "%s  /  %02d",
             kWeekdays[dt.date.weekDay], dt.date.date);
    frame.drawString(todayHeader, 164, 19);
    frame.setTextDatum(top_right);
    frame.drawString(hubStatusText(hubStatus), width - 120, 19);
    drawBattery(width);

    // Quiet Hara-style month header and calendar grid.
    frame.drawFastHLine(leftX, monthBottom, leftWidth, TFT_BLACK);
    frame.setFont(&fonts::FreeSans24pt7b);
    frame.setTextDatum(bottom_left);
    frame.drawString(kMonthNames[dt.date.month - 1], leftX, monthBottom - 10);
    char yearText[8];
    snprintf(yearText, sizeof(yearText), "%04d", dt.date.year);
    drawPixelText(frame, yearText, leftX + leftWidth, monthBottom - 23,
                  2, TFT_BLACK, middle_right);

    constexpr int columns = 7;
    constexpr int rows = 6;
    frame.setTextDatum(bottom_right);
    frame.setFont(&fonts::Font0);
    for (int column = 0; column < columns; ++column) {
        const int cellRight = leftX + (column + 1) * leftWidth / columns;
        frame.drawString(kWeekdayShort[column], cellRight - 8,
                         weekdaysBottom - 6);
    }

    frame.drawFastHLine(leftX, weekdaysBottom, leftWidth, TFT_BLACK);
    for (int row = 0; row <= rows; ++row) {
        const int y = weekdaysBottom + row * (datesBottom - weekdaysBottom) / rows;
        if (row == 0) {
            frame.drawFastHLine(leftX, y, leftWidth, TFT_BLACK);
        } else {
            for (int x = leftX; x < leftX + leftWidth; x += 2) {
                frame.drawPixel(x, y, TFT_BLACK);
            }
        }
    }

    const int firstWeekday = weekDayForDate(dt.date.year, dt.date.month, 1);
    const int monthDays = daysInMonth(dt.date.year, dt.date.month);
    for (int slot = 0; slot < columns * rows; ++slot) {
        const int day = slot - firstWeekday + 1;
        if (day < 1 || day > monthDays) continue;

        const int column = slot % columns;
        const int row = slot / columns;
        const int cellX = leftX + column * leftWidth / columns;
        const int cellRight = leftX + (column + 1) * leftWidth / columns;
        const int cellY = weekdaysBottom + row * (datesBottom - weekdaysBottom) / rows;
        const int cellBottom = weekdaysBottom + (row + 1) *
                               (datesBottom - weekdaysBottom) / rows;
        const bool isToday = day == dt.date.date;
        const uint8_t activityLevel =
            activityLevelForDate(dt.date.year, dt.date.month, day);

        if (!isToday) {
            drawActivityTile(cellX + 3, cellY + 3,
                             cellRight - cellX - 6,
                             cellBottom - cellY - 6, activityLevel);
        }
        if (isToday) {
            frame.drawRect(cellX + 3, cellY + 3,
                           cellRight - cellX - 6,
                           cellBottom - cellY - 6, TFT_BLACK);
        }
        char dayText[4];
        snprintf(dayText, sizeof(dayText), "%02d", day);
        drawPixelText(frame, dayText, cellRight - 8, cellY + 7, 2,
                      (!isToday && activityLevel >= 3) ? TFT_WHITE : TFT_BLACK,
                      top_right);
    }

    // Right column: time and aggregate activity, separated by one quiet rule.
    frame.drawFastVLine(rightBorderX, monthTop, datesBottom - monthTop, TFT_BLACK);
    frame.setTextDatum(bottom_left);
    frame.setFont(&fonts::Font0);
    frame.drawString("LOCAL TIME / 24 HOURS", rightX, kTimePanelY - 10);

    char timeText[8];
    snprintf(timeText, sizeof(timeText), "%02d:%02d",
             dt.time.hours, dt.time.minutes);
    frame.drawFastHLine(kTimePanelX, kTimePanelY, kTimePanelWidth, TFT_BLACK);
    frame.drawFastHLine(kTimePanelX, kTimePanelY + kTimePanelHeight - 1,
                        kTimePanelWidth, TFT_BLACK);
    drawPixelText(frame, timeText, rightX, kTimePanelY + kTimePanelHeight / 2,
                  6, TFT_BLACK, middle_left);

    char dateText[24];
    snprintf(dateText, sizeof(dateText), "%04d / %02d / %02d",
             dt.date.year, dt.date.month, dt.date.date);
    drawPixelText(frame, dateText, rightX, 254, 2, TFT_BLACK, middle_left);

    frame.setTextDatum(top_left);
    frame.setFont(&fonts::Font0);
    frame.drawString("ACTIVITY THIS MONTH", rightX, 286);

    const int activeDays = codexActivity.valid
        ? monthActiveDays(dt.date.year, dt.date.month) : 0;
    const int currentStreak = codexActivity.valid ? codexActivity.currentStreak : 0;
    const int longestStreak = codexActivity.valid ? codexActivity.longestStreak : 0;

    char activeText[4];
    snprintf(activeText, sizeof(activeText), "%02d", min(activeDays, 99));
    drawPixelText(frame, activeText, rightX, 315, 5, TFT_BLACK, top_left);
    frame.setFont(&fonts::Font0);
    frame.drawString("ACTIVE DAYS", rightX + 70, 324);
    char monthCaption[16];
    snprintf(monthCaption, sizeof(monthCaption), "IN %s", kMonthShort[dt.date.month - 1]);
    frame.drawString(monthCaption, rightX + 70, 340);

    frame.drawFastHLine(rightX, 367, rightEdge - rightX, TFT_BLACK);
    const int statX[] = {rightX, rightX + 99};
    const int statValues[] = {currentStreak, longestStreak};
    const char* const statLabels[] = {"CURRENT", "LONGEST"};
    for (int column = 0; column < 2; ++column) {
        frame.drawString(statLabels[column], statX[column], 380);
        char valueText[4];
        snprintf(valueText, sizeof(valueText), "%02d", min(statValues[column], 99));
        drawPixelText(frame, valueText, statX[column], 399, 3, TFT_BLACK, top_left);
        frame.drawString("STREAK", statX[column], 426);
    }

    frame.drawFastHLine(rightX, 444, rightEdge - rightX, TFT_BLACK);
    frame.drawString("112-DAY LOCAL AGGREGATE", rightX, 451);

}

void presentCalendar(const m5::rtc_datetime_t& dt, bool qualityRefresh) {
    drawCalendar(dt);
    M5.Display.setEpdMode(qualityRefresh ? epd_mode_t::epd_quality
                                         : epd_mode_t::epd_text);
    frame.pushSprite(0, 0);

    if (qualityRefresh) {
        textRefreshesSinceQuality = 0;
        Serial.println("Display refresh: epd_quality (ghosting cleanup)");
    } else {
        ++textRefreshesSinceQuality;
        Serial.println("Display refresh: epd_text");
    }
}

void presentTimeOnly(const m5::rtc_datetime_t& dt) {
    char timeText[8];
    snprintf(timeText, sizeof(timeText), "%02d:%02d",
             dt.time.hours, dt.time.minutes);

    timeFrame.fillSprite(TFT_WHITE);
    timeFrame.drawFastHLine(0, 0, kTimePanelWidth, TFT_BLACK);
    timeFrame.drawFastHLine(0, kTimePanelHeight - 1,
                            kTimePanelWidth, TFT_BLACK);
    drawPixelText(timeFrame, timeText, 6,
                  kTimePanelHeight / 2, 6, TFT_BLACK, middle_left);

    M5.Display.setEpdMode(epd_mode_t::epd_text);
    timeFrame.pushSprite(kTimePanelX, kTimePanelY);
    ++textRefreshesSinceQuality;
    Serial.println("Display refresh: time panel only (epd_text)");
}

void presentHubStatusOnly() {
    hubStatusFrame.fillSprite(TFT_WHITE);
    hubStatusFrame.setTextColor(TFT_BLACK);
    hubStatusFrame.setTextDatum(top_right);
    hubStatusFrame.setFont(&fonts::Font0);
    hubStatusFrame.setTextSize(1);
    hubStatusFrame.drawString(hubStatusText(hubStatus),
                              kHubStatusPanelWidth, 0);

    M5.Display.setEpdMode(epd_mode_t::epd_text);
    hubStatusFrame.pushSprite(kHubStatusPanelX, kHubStatusPanelY);
    ++textRefreshesSinceQuality;
    Serial.println("Display refresh: Hub status only (epd_text)");
}

void handleSerialTimeSet() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();

    int year, month, day, hour, minute, second;
    if (sscanf(line.c_str(), "SET %d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) == 6 &&
        isValidDateTime(year, month, day, hour, minute, second)) {
        const int weekDay = weekDayForDate(year, month, day);
        M5.Rtc.setDateTime({{year, month, day, weekDay},
                            {hour, minute, second}});
        lastMinuteKey = UINT32_MAX;
        forceQualityRefresh = true;
        Serial.printf("RTC set: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        return;
    }

    Serial.println("Use: SET YYYY-MM-DD HH:MM:SS");
}

}  // namespace

void setup() {
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);

    Serial.begin(115200);
    Serial.setTimeout(50);
    delay(100);
    Serial.println("PaperMono Calendar booting...");

    M5.Display.setRotation(1);
    M5.Display.setBrightness(kFrontlightBrightness);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    frame.setPsram(true);
    frame.setColorDepth(2);
    if (frame.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
        Serial.println("FATAL: full-screen canvas allocation failed");
        while (true) delay(1000);
    }
    Serial.printf("Canvas: %d x %d, 2-bit grayscale, PSRAM preferred\n",
                  frame.width(), frame.height());

    timeFrame.setPsram(true);
    timeFrame.setColorDepth(2);
    if (timeFrame.createSprite(kTimePanelWidth, kTimePanelHeight) == nullptr) {
        Serial.println("FATAL: time-panel canvas allocation failed");
        while (true) delay(1000);
    }

    hubStatusFrame.setPsram(true);
    hubStatusFrame.setColorDepth(2);
    if (hubStatusFrame.createSprite(kHubStatusPanelWidth,
                                    kHubStatusPanelHeight) == nullptr) {
        Serial.println("FATAL: Hub-status canvas allocation failed");
        while (true) delay(1000);
    }

    if (!M5.Rtc.isEnabled()) {
        Serial.println("FATAL: RX8130CE RTC not detected");
        frame.fillSprite(TFT_WHITE);
        frame.setTextDatum(middle_center);
        frame.setTextColor(TFT_BLACK);
        frame.setFont(&fonts::FreeSansBold18pt7b);
        frame.drawString("RTC NOT FOUND", frame.width() / 2, frame.height() / 2);
        frame.pushSprite(0, 0);
        while (true) delay(1000);
    }

    auto now = M5.Rtc.getDateTime();
    if (!rtcDateTimeIsValid(now) || M5.Rtc.getVoltLow()) {
        Serial.println("RTC invalid or voltage-low flag set; using build time");
        setRtcFromBuildTime();
        now = M5.Rtc.getDateTime();
    }

    presentCalendar(now, true);
    lastMinuteKey = minuteKey(now);
    lastDateKey = dateKey(now);
    Serial.println("Serial time command: SET YYYY-MM-DD HH:MM:SS");
}

void loop() {
    M5.update();
    handleSerialTimeSet();

    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - nextActivitySyncMs) >= 0) {
        // Show the first risky BLE stage before entering the stack. If the
        // controller blocks or resets, the e-paper retains this diagnosis.
        // This is a single small partial update, counted toward the 10:1 rule.
        if (hubStatus == HubStatus::Waiting) {
            hubStatus = HubStatus::Scanning;
            presentHubStatusOnly();
        }
        bool activityChanged = false;
        bool statusChanged = false;
        const bool synced = syncActivityViaBLE(activityChanged, statusChanged);
        nextActivitySyncMs = millis() +
            (synced ? kActivitySyncIntervalMs : kActivityRetryIntervalMs);
        if (activityChanged || statusChanged) {
            // Merge changed activity into one full-canvas push; retries alone stay silent.
            calendarDirty = true;
            lastMinuteKey = UINT32_MAX;
        }
    }

    if (nowMs - lastRtcPollMs < kRtcPollIntervalMs) {
        delay(20);
        return;
    }
    lastRtcPollMs = nowMs;

    const auto now = M5.Rtc.getDateTime();
    if (!rtcDateTimeIsValid(now)) {
        Serial.println("WARN: invalid RTC read ignored");
        return;
    }

    const uint32_t currentMinuteKey = minuteKey(now);
    if (currentMinuteKey == lastMinuteKey) return;

    const uint32_t currentDateKey = dateKey(now);
    const bool dateChanged = currentDateKey != lastDateKey;
    const bool periodicCleanup =
        textRefreshesSinceQuality >= kQualityRefreshInterval;
    const bool qualityRefresh = forceQualityRefresh || dateChanged || periodicCleanup;
    const bool fullCalendarRefresh = qualityRefresh || calendarDirty;

    if (fullCalendarRefresh) {
        presentCalendar(now, qualityRefresh);
        calendarDirty = false;
    } else {
        presentTimeOnly(now);
    }
    forceQualityRefresh = false;
    lastMinuteKey = currentMinuteKey;
    lastDateKey = currentDateKey;
}
