#include "lcd_i2c.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LCD_I2C";

#define PIN_RS 0x01
#define PIN_RW 0x02
#define PIN_EN 0x04
#define PIN_BL 0x08

#define CMD_CLEAR 0x01
#define CMD_ENTRY_MODE 0x06   /* incrementa cursor, sem shift */
#define CMD_DISPLAY_ON 0x0C   /* display on, cursor off, blink off */
#define CMD_DISPLAY_OFF 0x08
#define CMD_FUNCTION_SET 0x28 /* 4 bits, 2 linhas, 5x8 */
#define CMD_SET_DDRAM 0x80

#define I2C_TIMEOUT_MS 50

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_backlight = PIN_BL;
static bool s_ready;

/* Envia um nibble alto (bits 7..4 de `nibble`) com pulso em E. */
static esp_err_t write_nibble(uint8_t nibble, bool rs)
{
    const uint8_t ctrl = (uint8_t)(s_backlight | (rs ? PIN_RS : 0));
    const uint8_t hi = (uint8_t)(nibble & 0xF0);
    const uint8_t buf[2] = {(uint8_t)(hi | ctrl | PIN_EN), (uint8_t)(hi | ctrl)};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t write_byte(uint8_t value, bool rs)
{
    const uint8_t ctrl = (uint8_t)(s_backlight | (rs ? PIN_RS : 0));
    const uint8_t hi = (uint8_t)(value & 0xF0);
    const uint8_t lo = (uint8_t)((value << 4) & 0xF0);
    const uint8_t buf[4] = {
        (uint8_t)(hi | ctrl | PIN_EN), (uint8_t)(hi | ctrl),
        (uint8_t)(lo | ctrl | PIN_EN), (uint8_t)(lo | ctrl),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t write_cmd(uint8_t cmd)
{
    return write_byte(cmd, false);
}

esp_err_t lcd_i2c_init(const lcd_i2c_config_t *config)
{
    if (config == NULL || config->address < 0x08 || config->address > 0x77 || config->freq_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_probe(s_bus, config->address, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nenhum dispositivo I2C em 0x%02X (SDA=%d SCL=%d). Confira fiacao/endereco (0x27 ou 0x3F)",
                 config->address, config->sda_gpio, config->scl_gpio);
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = config->freq_hz,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }

    s_backlight = config->backlight ? PIN_BL : 0;

    /* Sequencia de inicializacao do HD44780 em modo 4 bits (datasheet fig. 24). */
    vTaskDelay(pdMS_TO_TICKS(50));
    err = write_nibble(0x30, false);
    if (err != ESP_OK) {
        goto fail;
    }
    esp_rom_delay_us(4500);
    write_nibble(0x30, false);
    esp_rom_delay_us(4500);
    write_nibble(0x30, false);
    esp_rom_delay_us(200);
    write_nibble(0x20, false); /* passa para 4 bits */
    esp_rom_delay_us(200);

    write_cmd(CMD_FUNCTION_SET);
    write_cmd(CMD_DISPLAY_OFF);
    write_cmd(CMD_CLEAR);
    esp_rom_delay_us(2000);
    write_cmd(CMD_ENTRY_MODE);
    err = write_cmd(CMD_DISPLAY_ON);
    if (err != ESP_OK) {
        goto fail;
    }

    s_ready = true;
    ESP_LOGI(TAG, "LCD 16x2 OK em 0x%02X (SDA=%d SCL=%d)", config->address, config->sda_gpio, config->scl_gpio);
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(s_dev);
    i2c_del_master_bus(s_bus);
    s_dev = NULL;
    s_bus = NULL;
    return err;
}

esp_err_t lcd_i2c_clear(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = write_cmd(CMD_CLEAR);
    esp_rom_delay_us(2000);
    return err;
}

esp_err_t lcd_i2c_set_backlight(bool on)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_backlight = on ? PIN_BL : 0;
    const uint8_t b = s_backlight;
    return i2c_master_transmit(s_dev, &b, 1, I2C_TIMEOUT_MS);
}

esp_err_t lcd_i2c_write_line(uint8_t row, const char *text)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (row >= LCD_I2C_ROWS || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = write_cmd((uint8_t)(CMD_SET_DDRAM | (row == 0 ? 0x00 : 0x40)));
    if (err != ESP_OK) {
        return err;
    }
    const size_t len = strlen(text);
    for (int col = 0; col < LCD_I2C_COLS; col++) {
        const char c = ((size_t)col < len) ? text[col] : ' ';
        err = write_byte((uint8_t)c, true);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
