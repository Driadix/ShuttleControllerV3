// Test relay firmware for the L4 sensor bench (ticket #73).
// LilyGo T-Display S3: receives controller network_bridge UART
// (230400 8E1, V1 display profile) on Serial1 (RX=21, TX=16, pins from
// the reference ShuttleDisplay firmware).
//   - Every received byte is relayed raw to USB CDC (host log capture).
//   - Status panel on the ST7789 170x320 panel: AS5600 health/angle and
//     per-role ToF distance/validity from MSG_SENSORS (0x02) and
//     MSG_AS5600_HEALTH (0x09). Logs stay on the COM channel; the panel
//     redraws only rows whose value changed (no flicker).
// Bench tooling only, not production firmware.

#include <Arduino.h>
#include <TFT_eSPI.h>

#define CTRL_BAUD 230400
#define RX1_PIN   21
#define TX1_PIN   16

#define SYNC1 0xBB
#define SYNC2 0xCC

#define MSG_SENSORS        0x02
#define MSG_REQ_SENSORS    0x05
#define MSG_AS5600_HEALTH  0x09
#define MSG_REQ_AS5600     0x0A
#define MSG_LOG            0x10

#define HW_FLAG_TOF_CH_F_VALID  (1U << 10)
#define HW_FLAG_TOF_CH_R_VALID  (1U << 11)
#define HW_FLAG_TOF_PAL_F_VALID (1U << 12)
#define HW_FLAG_TOF_PAL_R_VALID (1U << 13)
#define HW_FLAG_AS5600_VALID    (1U << 14)

#define HDR_LEN     6 // sync1 sync2 msgID targetID seq length
#define MAX_PAYLOAD 120 // PROTOCOL_MAX_FRAME_SIZE(128) - HDR_LEN(6) - CRC(2)
#define MAX_FRAME   128

#define TOF_ROLES 4

static const uint8_t kTofAddr[TOF_ROLES]    = {0x09, 0x0A, 0x0B, 0x0C};
static const char *const kTofRole[TOF_ROLES] = {"ChR", "ChF", "PlR", "PlF"};

static TFT_eSPI tft;
static uint32_t rxBytes = 0;
static uint32_t logFrames = 0;

// --- Sensor state (updated by frame parser) ---
static uint16_t asAngleRaw = 0;
static uint8_t  asFlags = 0;
static uint32_t asFailures = 0;
static uint32_t asSuccesses = 0;
static bool     hasAsHealth = false;
static uint16_t tofDist[TOF_ROLES]  = {0, 0, 0, 0};
static bool     tofValid[TOF_ROLES] = {false, false, false, false};
static bool     hasSensors = false;

// --- Frame parser (byte-driven state machine) ---
static uint8_t rxBuf[MAX_FRAME];
static size_t rxIndex = 0;
static size_t frameLen = 0;
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

// Send a request frame: 0xBB 0xCC msgID targetID=0 seq length=0 + CRC.
// SENSORS and AS5600_HEALTH are pushed only on request (Cntrl_V2.ino
// processPacket: MSG_REQ_SENSORS / MSG_REQ_AS5600_HEALTH).
static uint8_t reqSeq = 0;

static void sendRequest(uint8_t msgID)
{
    uint8_t f[8];
    f[0] = SYNC1;
    f[1] = SYNC2;
    f[2] = msgID;
    f[3] = 0x00; // TARGET_ID_NONE: direct UART line
    f[4] = reqSeq++;
    f[5] = 0x00; // length: empty payload
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 6; ++i)
        crc = updateCRC16(crc, f[i]);
    f[6] = (uint8_t)(crc & 0xFF);
    f[7] = (uint8_t)(crc >> 8);
    Serial1.write(f, sizeof(f));
    Serial1.flush();
}

static void renderSensorsFrame(void)
{
    // SensorPacket, 16 B little-endian: distF distR distPltF distPltR
    // angle lifterCurrent temperature_dC hardwareFlags
    uint8_t *p = rxBuf + HDR_LEN;
    tofDist[0] = (uint16_t)(p[0] | (p[1] << 8));   // ChR = distanceR? see roles below
    uint16_t distR    = (uint16_t)(p[2] | (p[3] << 8));
    uint16_t distPltF = (uint16_t)(p[4] | (p[5] << 8));
    uint16_t distPltR = (uint16_t)(p[6] | (p[7] << 8));
    uint16_t hwFlags  = (uint16_t)(p[14] | (p[15] << 8));

    // Role wiring: ChR=0x09<-distR, ChF=0x0A<-distF, PlR=0x0B<-distPltR,
    // PlF=0x0C<-distPltF
    tofDist[0] = distR;
    tofDist[1] = (uint16_t)(p[0] | (p[1] << 8));
    tofDist[2] = distPltR;
    tofDist[3] = distPltF;
    tofValid[0] = (hwFlags & HW_FLAG_TOF_CH_R_VALID) != 0;
    tofValid[1] = (hwFlags & HW_FLAG_TOF_CH_F_VALID) != 0;
    tofValid[2] = (hwFlags & HW_FLAG_TOF_PAL_R_VALID) != 0;
    tofValid[3] = (hwFlags & HW_FLAG_TOF_PAL_F_VALID) != 0;
    hasSensors = true;
}

static void renderAs5600Frame(void)
{
    // As5600HealthPacket, 21 B little-endian
    uint8_t *p = rxBuf + HDR_LEN;
    asAngleRaw = (uint16_t)(p[8] | (p[9] << 8));
    asFlags    = p[15];
    asFailures = (uint32_t)p[18];
    asSuccesses = (uint32_t)p[19];
    hasAsHealth = true;
}

static void renderLogFrame(void)
{
    uint8_t msgID = rxBuf[2];
    uint8_t len   = rxBuf[5];
    size_t total  = HDR_LEN + (size_t)len + 2;

    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < total - 2; ++i)
        crc = updateCRC16(crc, rxBuf[i]);
    if ((uint8_t)(crc & 0xFF) != rxBuf[total - 2] || (uint8_t)(crc >> 8) != rxBuf[total - 1])
        return; // corrupt frame: dropped (raw bytes still relayed)

    if (msgID == MSG_SENSORS && len >= 16)
    {
        renderSensorsFrame();
        return;
    }
    if (msgID == MSG_AS5600_HEALTH && len >= 21)
    {
        renderAs5600Frame();
        return;
    }
    if (msgID == MSG_LOG && len >= 2)
        ++logFrames;
}

static void parserFeed(uint8_t b)
{
    if (!haveLen)
    {
        rxBuf[rxIndex++] = b;
        if (rxIndex == 1)
        {
            if (b != SYNC1)
                rxIndex = 0;
        }
        else if (rxIndex == 2)
        {
            if (b != SYNC2)
            {
                rxBuf[0] = b;
                rxIndex = (b == SYNC1) ? 1 : 0;
            }
        }
        else if (rxIndex == HDR_LEN)
        {
            uint8_t len = rxBuf[5];
            if ((size_t)len > MAX_FRAME - HDR_LEN - 2)
            {
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

// --- Status panel (row-cached: only changed rows are redrawn) ---
#define PANEL_ROWS 6
#define PANEL_ROW_H 15

struct RowCache
{
    uint32_t bg;
    char     text[48];
    bool     dirty;
};

static RowCache rowCache[PANEL_ROWS];
static const int rowY[PANEL_ROWS] = {0, 18, 36, 52, 68, 84};

static void drawRow(int row, uint32_t bg, const char *fmt, ...)
{
    char buf[48];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    RowCache &c = rowCache[row];
    if (!c.dirty && c.bg == bg && strcmp(c.text, buf) == 0)
        return; // unchanged: keep the panel silent

    tft.fillRect(0, rowY[row], 320, PANEL_ROW_H, bg);
    tft.setCursor(2, rowY[row] + 1);
    tft.print(buf);
    c.bg    = bg;
    c.dirty = false;
    snprintf(c.text, sizeof(c.text), "%s", buf);
}

static void redrawPanel(void)
{
    char fl[24] = "-";
    size_t o = 0;
    if (asFlags & 0x01) o += (size_t)snprintf(fl + o, sizeof(fl) - o, "RESP ");
    if (asFlags & 0x02) o += (size_t)snprintf(fl + o, sizeof(fl) - o, "ANGLE ");
    if (asFlags & 0x04) o += (size_t)snprintf(fl + o, sizeof(fl) - o, "MAG ");
    if (asFlags & 0x08) o += (size_t)snprintf(fl + o, sizeof(fl) - o, "WEAK ");
    if (asFlags & 0x10) o += (size_t)snprintf(fl + o, sizeof(fl) - o, "STRONG ");

    drawRow(0, TFT_NAVY, "L4 relay %u bd 8E1 rx=%lu log=%lu",
            (unsigned)CTRL_BAUD, (unsigned long)rxBytes, (unsigned long)logFrames);

    if (hasAsHealth)
        drawRow(1, TFT_BLACK, "AS5600 raw=%u [%s] fail=%lu succ=%lu",
                (unsigned)asAngleRaw, fl, (unsigned long)asFailures,
                (unsigned long)asSuccesses);
    else
        drawRow(1, TFT_BLACK, "AS5600: waiting...");

    for (int i = 0; i < TOF_ROLES; ++i)
    {
        if (hasSensors)
        {
            if (tofValid[i])
                drawRow(2 + i, TFT_BLACK, "ToF %02X %s: %u mm",
                        (unsigned)kTofAddr[i], kTofRole[i], (unsigned)tofDist[i]);
            else
                drawRow(2 + i, TFT_MAROON, "ToF %02X %s: noack",
                        (unsigned)kTofAddr[i], kTofRole[i]);
        }
        else
        {
            drawRow(2 + i, TFT_BLACK, "ToF %02X %s: waiting...",
                    (unsigned)kTofAddr[i], kTofRole[i]);
        }
    }
}

void setup(void)
{
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
    redrawPanel();

    Serial1.begin(CTRL_BAUD, SERIAL_8E1, RX1_PIN, TX1_PIN);
    Serial1.setTimeout(100);
    Serial.begin(115200); // USB CDC: raw relay only, no banner
    redrawPanel();
}

void loop(void)
{
    // Periodic request polling: SENSORS (0x05) and AS5600_HEALTH (0x0A)
    // are pushed only on request; alternate every 500 ms.
    static uint32_t lastReqMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastReqMs >= 500)
    {
        lastReqMs = nowMs;
        sendRequest((nowMs / 500) & 1 ? MSG_REQ_SENSORS : MSG_REQ_AS5600);
    }

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
        redrawPanel();
    }

    redrawPanel();
}
