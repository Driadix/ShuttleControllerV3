// Test relay firmware for the L4 sensor bench (ticket #73).
// LilyGo T-Display S3: receives controller network_bridge UART
// (230400 8E1, V1 display profile) on Serial1 (RX=21, TX=16, pins from
// the reference ShuttleDisplay firmware).
//   - Every received byte is relayed raw to USB CDC (host log capture).
//   - MSG_LOG frames (0xBB 0xCC, msgID 0x10, payload level+text+NUL,
//     CRC16-CCITT LSB-first) are parsed and rendered as a log tail on
//     the ST7789 170x320 panel.
// Bench tooling only, not production firmware.

#include <Arduino.h>
#include <TFT_eSPI.h>

#define CTRL_BAUD 230400
#define RX1_PIN   21
#define TX1_PIN   16

#define SYNC1 0xBB
#define SYNC2 0xCC
#define MSG_LOG 0x10

#define HDR_LEN     6 // sync1 sync2 msgID targetID seq length
#define MAX_PAYLOAD 120 // PROTOCOL_MAX_FRAME_SIZE(128) - HDR_LEN(6) - CRC(2)
#define MAX_FRAME   128

static const size_t kLineMax = 64; // chars per display line
static const size_t kTailMax = 9;  // display lines kept (320x170, font 2)

static TFT_eSPI tft;
static char tailBuf[kTailMax][kLineMax + 1];
static uint8_t tailLen[kTailMax];
static size_t tailIdx = 0;
static uint32_t lastFrameMs = 0;
static uint32_t rxBytes = 0;
static uint32_t logFrames = 0;
static uint32_t crcErrors = 0;

// --- MSG_LOG frame parser (state machine, byte-driven) ---
static uint8_t rxBuf[MAX_FRAME];
static size_t rxIndex = 0;
static size_t frameLen = 0; // full frame length: HDR + payload + CRC
static bool haveLen = false;

static uint16_t updateCRC16(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (uint8_t j = 0; j < 8; ++j)
    {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

static void parserReset(void)
{
    rxIndex = 0;
    haveLen = false;
}

static void renderLogFrame(void)
{
    // rxBuf: [sync1 sync2 msgID targetID seq length] [level text... NUL] [crcL crcH]
    uint8_t msgID = rxBuf[2];
    uint8_t len   = rxBuf[5];
    size_t total  = HDR_LEN + (size_t)len + 2;

    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < total - 2; ++i)
        crc = updateCRC16(crc, rxBuf[i]);
    if ((uint8_t)(crc & 0xFF) != rxBuf[total - 2] || (uint8_t)(crc >> 8) != rxBuf[total - 1])
    {
        ++crcErrors;
        return;
    }

    if (msgID != MSG_LOG || len < 2)
        return;
    // payload: [level] [text... NUL]  (text may contain trailing garbage; stop at NUL)
    size_t textLen = 0;
    for (size_t i = 1; i < (size_t)len; ++i)
    {
        if (rxBuf[HDR_LEN + i] == 0)
            break;
        ++textLen;
    }
    if (textLen == 0)
        return;
    ++logFrames;
    size_t n = textLen > kLineMax ? kLineMax : textLen;
    for (size_t i = 0; i < n; ++i)
    {
        char c = (char)rxBuf[HDR_LEN + 1 + i];
        tailBuf[tailIdx][i] = (c >= 0x20) ? c : ' ';
    }
    tailBuf[tailIdx][n] = '\0';
    tailLen[tailIdx]    = (uint8_t)n;
    tailIdx             = (tailIdx + 1) % kTailMax;
}

static void parserFeed(uint8_t b)
{
    if (!haveLen)
    {
        // header phase
        rxBuf[rxIndex++] = b;
        if (rxIndex == 1)
        {
            if (b != SYNC1)
                rxIndex = 0; // keep waiting for sync1
        }
        else if (rxIndex == 2)
        {
            if (b != SYNC2)
            {
                // resync: retry the byte as potential sync1
                rxBuf[0] = b;
                rxIndex = (b == SYNC1) ? 1 : 0;
            }
        }
        else if (rxIndex == HDR_LEN)
        {
            uint8_t len = rxBuf[5];
            if ((size_t)len > MAX_FRAME - HDR_LEN - 2)
            {
                // corrupted length: drop frame, keep last byte as sync1 candidate
                rxBuf[0] = rxBuf[HDR_LEN - 1];
                rxIndex  = (rxBuf[0] == SYNC1) ? 1 : 0;
                haveLen  = false;
                return;
            }
            frameLen = HDR_LEN + (size_t)len + 2;
            haveLen  = true;
        }
    }
    else
    {
        rxBuf[rxIndex++] = b;
        if (rxIndex >= frameLen)
        {
            renderLogFrame();
            parserReset();
        }
    }
}

static void tftHeader(void)
{
    tft.fillRect(0, 0, 320, 16, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(2, 3);
    tft.printf("L4 relay %u bd 8E1", (unsigned)CTRL_BAUD);
    tft.setCursor(190, 3);
    tft.printf("rx=%lu", (unsigned long)rxBytes);
    tft.setCursor(248, 3);
    tft.printf("log=%lu", (unsigned long)logFrames);
}

static void tftTail(void)
{
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.fillRect(0, 16, 320, 170 - 16, TFT_BLACK);
    for (size_t i = 0; i < kTailMax; ++i)
    {
        size_t idx = (tailIdx + kTailMax - 1 - i) % kTailMax; // newest first
        if (tailLen[idx] == 0)
            continue;
        tft.setCursor(2, 18 + (int)(i * 16));
        tft.print(tailBuf[idx]);
    }
}

static void refreshPanel(bool force)
{
    uint32_t now = millis();
    if (!force && (now - lastFrameMs < 200))
        return;
    lastFrameMs = now;
    tftHeader();
    tftTail();
}

void setup(void)
{
    // Panel power sequencing mirrors reference setupDisplay():
    // POWER_ON (GPIO15) HIGH after 100 ms, then backlight, then init.
    pinMode(15, OUTPUT);
    pinMode(TFT_BL, OUTPUT);
    delay(100);
    digitalWrite(15, HIGH);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(3);
    tft.setSwapBytes(true);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextFont(2);
    tft.fillScreen(TFT_BLACK);
    refreshPanel(true);

    Serial1.begin(CTRL_BAUD, SERIAL_8E1, RX1_PIN, TX1_PIN);
    Serial1.setTimeout(100);
    Serial.begin(115200); // USB CDC: raw relay only, no banner
    refreshPanel(true);
}

void loop(void)
{
    int n = Serial1.available();
    if (n > 0)
    {
        uint8_t chunk[64];
        while (n > 0)
        {
            int take = n > (int)sizeof(chunk) ? (int)sizeof(chunk) : n;
            int got  = Serial1.readBytes(chunk, take);
            if (got > 0)
            {
                Serial.write(chunk, (size_t)got); // raw relay to CDC
                for (int i = 0; i < got; ++i)
                    parserFeed(chunk[i]);
                rxBytes += (uint32_t)got;
            }
            n = Serial1.available();
        }
        refreshPanel(false);
    }

    refreshPanel(false);
}
