#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "driver/i2s_types.h"
#include "es8311_codec.h"
#include "esp_codec_dev_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "bsp_audiomgr.h"

#define MIC_FILE_PATH "/data/wav"

static const char *TAG = "BSP_AUDIOMGR";
static const char err_reason[][30] =
{
    "input param is invalid",
    "operation timeout"
};

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;

typedef struct _T_AUDIO_CTRL_DATA
{
    FILE *wav_file;
    volatile bool is_mic_start;
    volatile uint32_t wav_size;
    volatile uint32_t wav_written;
}T_AUDIO_CTRL_DATA;
static T_AUDIO_CTRL_DATA s_audio_ctrl_data =
{
    .wav_file = NULL,
    .is_mic_start = false,
    .wav_size = 0,
    .wav_written = 0,
};

static esp_err_t codec_init(void)
{
    /* Initialize I2C peripheral */
    i2c_master_bus_handle_t i2c_bus_handle = NULL;
    i2c_master_bus_config_t i2c_mst_cfg = 
    {
        .i2c_port = CONFIG_BSP_AUDIO_I2C_NUM,
        .sda_io_num = CONFIG_BSP_AUDIO_I2C_SDA_PIN,
        .scl_io_num = CONFIG_BSP_AUDIO_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* Pull-up internally for no external pull-up case.
        Suggest to use external pull-up to ensure a strong enough pull-up. */
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_cfg, &i2c_bus_handle));

    /* Create control interface with I2C bus handle */
    audio_codec_i2c_cfg_t i2c_cfg = 
    {
        .port = CONFIG_BSP_AUDIO_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if);


    /* Create data interface with I2S bus handle */
    audio_codec_i2s_cfg_t i2s_cfg =
    {
        .port = CONFIG_BSP_AUDIO_I2S_NUM,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };

    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if);

    /* Create ES8311 interface handle */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    assert(gpio_if);
    es8311_codec_cfg_t es8311_cfg =
    {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
        .master_mode = false,
        .use_mclk = CONFIG_BSP_AUDIO_I2S_MCLK_PIN >= 0,
        .pa_pin = CONFIG_BSP_AUDIO_PA_CTRL_PIN,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = CONFIG_BSP_AUDIO_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    assert(es8311_if);

    /* Create the top codec handle with ES8311 interface handle and data interface */
    esp_codec_dev_cfg_t dev_cfg =
    {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t codec_handle = esp_codec_dev_new(&dev_cfg);
    assert(codec_handle);

    /* Specify the sample configurations and open the device */
    esp_codec_dev_sample_info_t sample_cfg =
    {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = CONFIG_BSP_AUDIO_SAMPLE_RATE,
        .mclk_multiple = CONFIG_BSP_AUDIO_MCLK_MULTIPLE,
    };
    if (esp_codec_dev_open(codec_handle, &sample_cfg) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "Failed to open codec device");
        return ESP_FAIL;
    }

    /* Set the initial volume and mic gain */
    if (esp_codec_dev_set_out_vol(codec_handle, CONFIG_BSP_AUDIO_VOICE_VOLUME) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "set output volume failed");
        return ESP_FAIL;
    }
    if (esp_codec_dev_set_in_gain(codec_handle, CONFIG_BSP_AUDIO_MIC_GAIN) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "set input gain failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t i2s_driver_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg =
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_BSP_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = 
        {
            .mclk = CONFIG_BSP_AUDIO_I2S_MCLK_PIN,
            .bclk = CONFIG_BSP_AUDIO_I2S_SCLK_PIN,
            .ws = CONFIG_BSP_AUDIO_I2S_LRCK_PIN,
            /* ASDOUT: codec ADC -> MCU DIN; DSDIN: MCU DOUT -> codec DAC */
            .dout = CONFIG_BSP_AUDIO_I2S_DSDIN_PIN,
            .din = CONFIG_BSP_AUDIO_I2S_ASDOUT_PIN,
            .invert_flags = 
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    return ESP_OK;
}

static void i2s_task(void *args)
{
    const size_t buf_bytes = 2400;
    int16_t *mic_data = malloc(buf_bytes);
    if (!mic_data) {
        ESP_LOGE(TAG, "No memory for read data buffer");
        abort();
    }

    esp_err_t ret = ESP_OK;
    size_t bytes_read = 0;
    size_t bytes_write = 0;
    int log_div = 0;
    ESP_LOGI(TAG, "i2s start (print RX stats; speak into mic to see change)");

    /* Ensure /data/wav exists (create if missing) */
    struct stat st;
    if (stat(MIC_FILE_PATH, &st) == 0) 
    {
        if (!S_ISDIR(st.st_mode)) 
        {
            ESP_LOGE(TAG, "%s exists but is not a directory", MIC_FILE_PATH);
            abort();
        }
    } 
    else if (mkdir(MIC_FILE_PATH, 0775) != 0 && errno != EEXIST) 
    {
        ESP_LOGE(TAG, "mkdir %s failed: errno=%d", MIC_FILE_PATH, errno);
        abort();
    }

    while (1) 
    {
        memset(mic_data, 0, buf_bytes);
        ret = i2s_channel_read(rx_handle, mic_data, buf_bytes, &bytes_read, 1000);

        /* If mic is not started, skip */
        if (!s_audio_ctrl_data.is_mic_start)
        {
            continue;
        }

        // write to wav file
        if(fwrite(mic_data, bytes_read, 1, s_audio_ctrl_data.wav_file) != 1)
        {
            ESP_LOGE(TAG, "fwrite failed: errno=%d (%s)", errno, strerror(errno));
        }

        s_audio_ctrl_data.wav_written += bytes_read;

        if(s_audio_ctrl_data.wav_written >= s_audio_ctrl_data.wav_size)
        {
            s_audio_ctrl_data.is_mic_start = false;
            fclose(s_audio_ctrl_data.wav_file);
            s_audio_ctrl_data.wav_file = NULL;
            ESP_LOGI(TAG, "mic recording done");
            s_audio_ctrl_data.wav_written = 0;
            s_audio_ctrl_data.wav_size = 0;
        }


        if (ret != ESP_OK) 
        {
            ESP_LOGE(TAG, "i2s read failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
            abort();
        }

        /* Observe whether I2S RX carries real PCM (quiet ~0, speak -> peak rises) */
        if (++log_div >= 20) 
        {
            log_div = 0;
            const size_t nsamples = bytes_read / sizeof(int16_t);
            int16_t min_v = 32767;
            int16_t max_v = -32768;
            int32_t sum_abs = 0;
            for (size_t i = 0; i < nsamples; i++) 
            {
                int16_t v = mic_data[i];
                if (v < min_v) 
                {
                    min_v = v;
                }
                if (v > max_v) 
                {
                    max_v = v;
                }
                sum_abs += (v < 0) ? -v : v;
            }
            int32_t avg_abs = nsamples ? (sum_abs / (int32_t)nsamples) : 0;
            int32_t peak = (max_v > -min_v) ? max_v : -min_v;
            ESP_LOGI(TAG, "I2S RX bytes=%u samples=%u min=%d max=%d avg_abs=%ld peak=%ld",
                     (unsigned)bytes_read, (unsigned)nsamples,
                     (int)min_v, (int)max_v, (long)avg_abs, (long)peak);
        }

        /* Keep echo so speaker also confirms the path */
        ret = i2s_channel_write(tx_handle, mic_data, bytes_read, &bytes_write, 1000);
        if (ret != ESP_OK) 
        {
            ESP_LOGE(TAG, "i2s write failed, %s", err_reason[ret == ESP_ERR_TIMEOUT]);
            abort();
        }
        if (bytes_read != bytes_write) 
        {
            ESP_LOGW(TAG, "%u bytes read but only %u bytes written",
                     (unsigned)bytes_read, (unsigned)bytes_write);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t audio_mic_start(int duration_sec)
{
    // create wav file
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    char *s_audio_file_path = calloc(sizeof(MIC_FILE_PATH) + 64, sizeof(char));
    if (!s_audio_file_path)
    {
        ESP_LOGE(TAG, "No memory for wav file path");
        return ESP_FAIL;
    }

    strftime(s_audio_file_path, sizeof(MIC_FILE_PATH) + 64, MIC_FILE_PATH "/rec_%Y%m%d_%H%M%S.wav", &t);

    //calculate wav size for duration_sec
    uint32_t byte_rate = CONFIG_BSP_AUDIO_SAMPLE_RATE * 2 * I2S_DATA_BIT_WIDTH_16BIT / 8;
    s_audio_ctrl_data.wav_size = byte_rate * duration_sec;
    const uint64_t need_bytes = (uint64_t)s_audio_ctrl_data.wav_size + sizeof(wav_header_t);

    /* FAT VFS does not implement statvfs(); use esp_vfs_fat_info() */
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t fs_err = esp_vfs_fat_info("/data", &total_bytes, &free_bytes);
    if (fs_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_info failed: %s", esp_err_to_name(fs_err));
        free(s_audio_file_path);
        return fs_err;
    }
    ESP_LOGI(TAG, "disk total=%llu MB, free=%llu MB, need=%llu bytes for %d s",
             (unsigned long long)(total_bytes / (1024 * 1024)),
             (unsigned long long)(free_bytes / (1024 * 1024)),
             (unsigned long long)need_bytes,
             duration_sec);
    if (free_bytes < need_bytes) {
        ESP_LOGE(TAG, "not enough space: free=%llu need=%llu",
                 (unsigned long long)free_bytes, (unsigned long long)need_bytes);
        free(s_audio_file_path);
        return ESP_ERR_NO_MEM;
    }

    /* Open wav file */
    s_audio_ctrl_data.wav_file = fopen(s_audio_file_path, "wb");
    free(s_audio_file_path);
    if (!s_audio_ctrl_data.wav_file)
    {
        ESP_LOGE(TAG, "create wav file failed");
        return ESP_FAIL;
    }

    const wav_header_t header = WAV_HEADER_PCM_DEFAULT(s_audio_ctrl_data.wav_size, I2S_DATA_BIT_WIDTH_16BIT, CONFIG_BSP_AUDIO_SAMPLE_RATE, 2);
    fwrite(&header, sizeof(wav_header_t), 1, s_audio_ctrl_data.wav_file);

    s_audio_ctrl_data.is_mic_start = true;
    s_audio_ctrl_data.wav_written = 0;

    return ESP_OK;
}

extern esp_err_t audio_cmd_init(void);

esp_err_t bsp_audiomgr_init(void)
{
    if(i2s_driver_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s driver init failed");
        return ESP_FAIL;
    }
    else 
    {
        ESP_LOGI(TAG, "i2s driver init success");
    }

    if(codec_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "codec init failed");
        return ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "codec init success");
    }

    if(audio_cmd_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "audio cmd init failed");
        return ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "audio cmd init success");
    }

    xTaskCreate(i2s_task, "i2s_task", 8192, NULL, 10, NULL);

    return ESP_OK;
}
