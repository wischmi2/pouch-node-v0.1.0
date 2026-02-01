/* Mikroe pH2 Click sensor handling for XIAO nRF52840 node */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "ph2_sensor.h"

/* Use the on-board I2C1 bus, which is routed to the XIAO connector. */
#define PH2_I2C_BUS_NODE DT_NODELABEL(i2c1)

/* Default MCP3221 address used on many Mikroe boards; adjust if needed. */
#define PH2_MCP3221_I2C_ADDR 0x4D

/* Period between pH reports (seconds). */
#define PH2_REPORT_PERIOD_S 10

/* Buffer size: 24 hours worth of readings at 10-second intervals */
#define PH2_BUFFER_SIZE 144

struct ph2_reading {
    uint32_t timestamp;  /* Seconds since boot */
    float ph;
    uint16_t raw;
};

static struct {
    struct ph2_reading buffer[PH2_BUFFER_SIZE];
    uint16_t head;       /* Next write position */
    uint16_t count;      /* Number of readings stored */
    bool full;           /* Buffer full flag */
} ph2_buffer = {0};

static const struct device *ph2_i2c;
static bool pouch_session_active;

/* Simple two-point calibration: pH = slope * raw + offset */
static bool have_low_point;
static bool have_high_point;
static float cal_ph_low;
static float cal_ph_high;
static uint16_t cal_raw_low;
static uint16_t cal_raw_high;
static float cal_slope;
static float cal_offset = 7.0f; /* reasonable default near neutral */
static bool ph_calibrated;

static int ph2_sensor_read_raw(uint16_t *out_raw)
{
    if (!ph2_i2c) {
        return -ENODEV;
    }

    uint8_t buf[2];
    int err = i2c_read(ph2_i2c, buf, sizeof(buf), PH2_MCP3221_I2C_ADDR);
    if (err) {
        LOG_ERR("pH sensor I2C read failed (err %d)", err);
        return err;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    /* MCP3221 is 12-bit left-justified in 16-bit word */
    raw >>= 4;

    *out_raw = raw;
    return 0;
}

static void ph2_update_calibration(void)
{
    if (!have_low_point || !have_high_point) {
        return;
    }

    if (cal_raw_high == cal_raw_low) {
        LOG_WRN("pH calibration points have identical raw values; ignoring");
        return;
    }

    cal_slope = (cal_ph_high - cal_ph_low) /
                (float)(cal_raw_high - cal_raw_low);
    cal_offset = cal_ph_low - cal_slope * (float)cal_raw_low;
    ph_calibrated = true;

    LOG_INF("pH calibration updated: slope=%f offset=%f",
            (double)cal_slope,
            (double)cal_offset);
}

static float ph2_from_raw(uint16_t raw)
{
    if (ph_calibrated) {
        return cal_slope * (float)raw + cal_offset;
    }

    /* Fallback: map full ADC range roughly to pH 0-14. */
    float approx_slope = 14.0f / 4095.0f;
    return approx_slope * (float)raw;
}

static void ph2_buffer_add_reading(float ph, uint16_t raw)
{
    struct ph2_reading *reading = &ph2_buffer.buffer[ph2_buffer.head];
    
    reading->timestamp = k_uptime_get_32() / 1000;  /* Seconds since boot */
    reading->ph = ph;
    reading->raw = raw;
    
    ph2_buffer.head = (ph2_buffer.head + 1) % PH2_BUFFER_SIZE;
    
    if (ph2_buffer.full) {
        /* Overwrite oldest reading when buffer is full */
        LOG_DBG("pH buffer full, overwriting oldest reading");
    } else {
        ph2_buffer.count++;
        if (ph2_buffer.count == PH2_BUFFER_SIZE) {
            ph2_buffer.full = true;
        }
    }
}

static void ph2_upload_buffered_readings(void)
{
    if (ph2_buffer.count == 0) {
        return;  /* Nothing to send */
    }
    
    /* Send readings in batches to avoid large payloads */
    const int max_batch_size = 10;  /* Adjust based on MTU/memory constraints */
    uint16_t start_idx = ph2_buffer.full ? ph2_buffer.head : 0;
    uint16_t readings_to_send = ph2_buffer.count;
    uint16_t sent = 0;
    
    while (sent < readings_to_send) {
        int batch_size = MIN(max_batch_size, readings_to_send - sent);
        char payload[512];  /* Adjust size based on batch_size */
        int len = snprintk(payload, sizeof(payload), "{\"readings\":[");
        
        for (int i = 0; i < batch_size && len < sizeof(payload) - 50; i++) {
            uint16_t idx = (start_idx + sent + i) % PH2_BUFFER_SIZE;
            struct ph2_reading *r = &ph2_buffer.buffer[idx];
            
            int added = snprintk(&payload[len], sizeof(payload) - len,
                                "%s{\"ts\":%u,\"ph\":%.3f,\"raw\":%u}",
                                i == 0 ? "" : ",",
                                r->timestamp,
                                (double)r->ph,
                                (unsigned int)r->raw);
            
            if (added < 0 || len + added >= sizeof(payload) - 10) {
                LOG_WRN("pH batch payload too large, truncating");
                break;
            }
            len += added;
        }
        
        /* Close JSON array and object */
        int close_len = snprintk(&payload[len], sizeof(payload) - len, 
                                 "],\"batch\":%d,\"total\":%u}", 
                                 sent / max_batch_size, 
                                 (unsigned int)readings_to_send);
        if (close_len > 0) {
            len += close_len;
        }
        
        int err = pouch_uplink_entry_write(".s/ph2_batch",
                                           POUCH_CONTENT_TYPE_JSON,
                                           payload,
                                           (size_t)len,
                                           K_NO_WAIT);
        if (err) {
            LOG_WRN("pH batch upload failed (err %d), will retry later", err);
            return;  /* Don't clear buffer on failure */
        }
        
        sent += batch_size;
        LOG_DBG("Uploaded pH batch %d/%d (%d readings)", 
                sent / max_batch_size, 
                (readings_to_send + max_batch_size - 1) / max_batch_size,
                batch_size);
    }
    
    /* Clear buffer after successful upload */
    ph2_buffer.count = 0;
    ph2_buffer.head = 0;
    ph2_buffer.full = false;
    
    LOG_INF("Successfully uploaded %u buffered pH readings", readings_to_send);
}

static void ph2_report_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(ph2_report_work, ph2_report_work_handler);

static void ph2_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        /* Try again later */
        k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));
        return;
    }

    float ph = ph2_from_raw(raw);

    /* Always log to the serial console so readings are visible even
     * when Pouch/gateway is not working. Avoid float formatting in
     * logs (which may be disabled) by using fixed-point.
     */
    int32_t ph_milli = (int32_t)(ph * 1000.0f);
    int32_t ph_int = ph_milli / 1000;
    int32_t ph_frac = ph_milli >= 0 ? (ph_milli % 1000) : -(ph_milli % 1000);

    LOG_INF("pH reading: ph=%d.%03d raw=%u (buffered: %u)",
            (int)ph_int,
            (int)ph_frac,
            (unsigned int)raw,
            (unsigned int)ph2_buffer.count);

    /* Always buffer the reading for later upload */
    ph2_buffer_add_reading(ph, raw);

    /* If connected, try to upload buffered readings */
    if (pouch_session_active) {
        ph2_upload_buffered_readings();
    }

    k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));
}

int ph2_sensor_init(void)
{
    ph2_i2c = DEVICE_DT_GET(PH2_I2C_BUS_NODE);
    if (!device_is_ready(ph2_i2c)) {
        LOG_ERR("pH sensor I2C bus not ready");
        return -ENODEV;
    }

    LOG_INF("pH2 sensor initialized on I2C bus %s addr 0x%02x",
            ph2_i2c->name,
            PH2_MCP3221_I2C_ADDR);

    /* Start periodic local logging immediately; uplink is optional. */
    k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));

    return 0;
}

void ph2_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
    LOG_INF("pH sensor: Pouch session started, uploading %u buffered readings", 
            (unsigned int)ph2_buffer.count);
    
    /* Immediately try to upload any buffered readings */
    if (ph2_buffer.count > 0) {
        ph2_upload_buffered_readings();
    }
}

void ph2_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
    LOG_INF("pH sensor: Pouch session ended, %u readings buffered for next connection", 
            (unsigned int)ph2_buffer.count);
}

int ph2_sensor_calibrate_low(float known_ph)
{
    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        return err;
    }

    cal_ph_low = known_ph;
    cal_raw_low = raw;
    have_low_point = true;

    LOG_INF("Captured low pH calibration point: ph=%.3f raw=%u",
            (double)known_ph,
            (unsigned int)raw);

    ph2_update_calibration();
    return 0;
}

int ph2_sensor_calibrate_high(float known_ph)
{
    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        return err;
    }

    cal_ph_high = known_ph;
    cal_raw_high = raw;
    have_high_point = true;

    LOG_INF("Captured high pH calibration point: ph=%.3f raw=%u",
            (double)known_ph,
            (unsigned int)raw);

    ph2_update_calibration();
    return 0;
}

int ph2_sensor_guided_calibration(float low_ph, float high_ph)
{
    int err;

    LOG_INF("Starting guided pH calibration: low=%f high=%f",
            (double)low_ph,
            (double)high_ph);

    err = ph2_sensor_calibrate_low(low_ph);
    if (err) {
        LOG_ERR("Failed low-point pH calibration (err %d)", err);
        return err;
    }

    err = ph2_sensor_calibrate_high(high_ph);
    if (err) {
        LOG_ERR("Failed high-point pH calibration (err %d)", err);
        return err;
    }

    LOG_INF("Guided pH calibration complete");
    return 0;
}

int ph2_sensor_get_buffer_status(uint16_t *count, uint16_t *capacity, bool *full)
{
    if (count) *count = ph2_buffer.count;
    if (capacity) *capacity = PH2_BUFFER_SIZE;
    if (full) *full = ph2_buffer.full;
    return 0;
}

int ph2_sensor_force_upload(void)
{
    if (!pouch_session_active) {
        return -ENOTCONN;  /* Not connected */
    }
    
    if (ph2_buffer.count == 0) {
        return -ENODATA;   /* No data to upload */
    }
    
    ph2_upload_buffered_readings();
    return 0;
}
