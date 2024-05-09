#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "Failsafe.h"
#include "Storage.h"

static const char *TAG = "Storage";
static nvs_handle_t nvsHandle;

bool Storage::Init()
{
    size_t required_size = 0;
    m_StorageData.ConfigMode = true;

    if (!CalculateMask())
    {
        return false;
    }

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE(TAG, "NVS initialization failed");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    err = nvs_open(TAG, NVS_READWRITE, &nvsHandle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS opening failed");
        return false;
    }

    nvs_get_blob(nvsHandle, TAG, nullptr, &required_size);
    nvs_get_blob(nvsHandle, TAG, &m_StorageData, &required_size);

    return true;
}

bool Storage::MountSPIFFS(const char *base_path, const char *partition_label)
{
    ESP_LOGI(TAG, "Mounting partition %s with base path: %s", base_path, partition_label);

    esp_vfs_spiffs_conf_t storage_config = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&storage_config);

    if (err != ESP_OK)
    {
        if (err == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Mounting or formatting failed");
        }

        else if (err == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Finding partition failed");
        }

        else
        {
            ESP_LOGE(TAG, "Initialization failed");
        }

        return false;
    }

#ifdef UNIT_DEBUG
    ESP_LOGI(TAG, "Performing check");

    err = esp_spiffs_check(storage_config.partition_label);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Spiffs check failed: %s", esp_err_to_name(err));

        return false;
    }

    ESP_LOGI(TAG, "Check successful");
#endif

    size_t total = 0, used = 0;
    err = esp_spiffs_info(storage_config.partition_label, &total, &used);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Getting partition information failed: %s - formatting", esp_err_to_name(err));
        esp_spiffs_format(storage_config.partition_label);

        return false;
    }

    else
    {
        ESP_LOGI(TAG, "Partition info: Total: %u - Used: %u", total, used);
    }

    return true;
}

bool Storage::Commit()
{
    ESP_LOGI(TAG, "Saving to storage blob");

    esp_err_t err = nvs_set_blob(nvsHandle, TAG, &m_StorageData, sizeof(StorageData));

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS setting blob failed");
        return false;
    }

    err = nvs_commit(nvsHandle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS committing blob failed");
        return false;
    }

    return true;
}

bool Storage::Reset()
{
    ESP_LOGI(TAG, "Resetting storage blob");

    m_StorageData = {};
    m_StorageData.ConfigMode = true;

    return Commit();
}

bool Storage::CalculateMask()
{
    ESP_LOGI(TAG, "Calculating encryption mask");

    uint32_t maskArray[10] = {};
    uint32_t macArray[10] = {};
    uint32_t mask = 1564230594;
    uint64_t mask2 = 0;

    union
    {
        uint64_t NumberRepresentation;
        uint8_t ArrayRepresentation[6];
    } mac = {};

    if (esp_efuse_mac_get_default(mac.ArrayRepresentation) != ESP_OK)
    {
        ESP_LOGE(TAG, "Reading mac from efuse failed");
        return false;
    }

    mask2 = mac.NumberRepresentation;

    for (int i = 10 - 1; i >= 0; i--)
    {
        macArray[i] = mask2 % 10;
        mask2 /= 10;
    }

    for (int i = 10 - 1; i >= 0; i--)
    {
        maskArray[i] = mask % 10;
        mask /= 10;
    }

    for (int i = 0; i <= 10 - 1; i += 2)
    {
        m_EncryptionMask *= 10;
        m_EncryptionMask += macArray[i];
    }

    for (int i = 9; i >= 1; i -= 2)
    {
        m_EncryptionMask *= 10;
        m_EncryptionMask += maskArray[i];
    }

    return true;
}

void Storage::EncryptText(uint32_t *var, const std::string &str)
{
    for (const char &c : str)
    {
        *(var++) = c ^ m_EncryptionMask;
    }
}

void Storage::DecryptText(uint32_t *var, std::string &str)
{
    str.clear();

    while (*var)
    {
        str.push_back(*(var++) ^ m_EncryptionMask);
    }
}

void Storage::GetSSID(std::string &str)
{
    str.reserve(SSIDLength);
    DecryptText(m_StorageData.SSID, str);
    ESP_LOGI(TAG, "Reading SSID: %s", str.c_str());
}

void Storage::GetPassword(std::string &str)
{
    str.reserve(PasswordLength);
    DecryptText(m_StorageData.Password, str);
    ESP_LOGI(TAG, "Reading Password: %s", str.c_str());
}

void Storage::GetDeviceId(std::string &str)
{
    str.reserve(UUIDLength);
    DecryptText(m_StorageData.DeviceId, str);
    ESP_LOGI(TAG, "Reading Device Id: %s", str.c_str());
}

void Storage::GetDeviceName(std::string &str)
{
    str.reserve(UUIDLength);
    DecryptText(m_StorageData.DeviceName, str);
    ESP_LOGI(TAG, "Reading Device Name: %s", str.c_str());
}

void Storage::GetAuthKey(std::string &str)
{
    str.reserve(UUIDLength);
    DecryptText(m_StorageData.AuthKey, str);
    ESP_LOGI(TAG, "Reading Auth Key: %s", str.c_str());
}

void Storage::GetAddress(std::string &str)
{
    str.reserve(EndpointLength);
    DecryptText(m_StorageData.Address, str);
    ESP_LOGI(TAG, "Reading Address: %s", str.c_str());
}

void Storage::GetLoudnessThreshold(int &threshold)
{
    threshold = m_StorageData.LoudnessThreshold;
    ESP_LOGI(TAG, "Loudness Threshold: %d", m_StorageData.LoudnessThreshold);
}

void Storage::GetRegisterInterval(int &interval)
{
    interval = m_StorageData.RegisterInterval;
    ESP_LOGI(TAG, "Register Interval: %d", m_StorageData.RegisterInterval);
}

bool Storage::GetEnabledSensors(Backend::Menus sensor)
{
    return m_StorageData.EnabledSensors[sensor];
}

bool Storage::GetConfigMode()
{
    ESP_LOGI(TAG, "Config Mode: %s", m_StorageData.ConfigMode == true ? "True" : "False");
    return m_StorageData.ConfigMode;
}

void Storage::SetSSID(const std::string &str)
{
    if (str.length() <= SSIDLength)
    {
        ESP_LOGI(TAG, "Setting SSID: %s", str.c_str());
        EncryptText(m_StorageData.SSID, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "SSID too long"});
    }
}

void Storage::SetPassword(const std::string &str)
{
    if (str.length() <= PasswordLength)
    {
        ESP_LOGI(TAG, "Setting Password: %s", str.c_str());
        EncryptText(m_StorageData.Password, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "Password too long"});
    }
}

void Storage::SetDeviceId(const std::string &str)
{
    if (str.length() <= UUIDLength)
    {
        ESP_LOGI(TAG, "Setting Device Id: %s", str.c_str());
        EncryptText(m_StorageData.DeviceId, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "Device Id too long"});
    }
}

void Storage::SetDeviceName(const std::string &str)
{
    if (str.length() <= UUIDLength)
    {
        ESP_LOGI(TAG, "Setting Device Name: %s", str.c_str());
        EncryptText(m_StorageData.DeviceName, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "Device Name too long"});
    }
}

void Storage::SetAuthKey(const std::string &str)
{
    if (str.length() <= UUIDLength)
    {
        ESP_LOGI(TAG, "Setting Auth Key: %s", str.c_str());
        EncryptText(m_StorageData.AuthKey, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "Auth Key too long"});
    }
}

void Storage::SetAddress(const std::string &str)
{
    if (str.length() <= EndpointLength)
    {
        ESP_LOGI(TAG, "Setting Address: %s", str.c_str());
        EncryptText(m_StorageData.Address, str);
    }

    else
    {
        Failsafe::AddFailure({.Message = "Address too long"});
    }
}

void Storage::SetLoudnessThreshold(int threshold)
{
    ESP_LOGI(TAG, "Setting Loudness Threshold: %d", threshold);
    m_StorageData.LoudnessThreshold = threshold;
}

void Storage::SetRegisterInterval(int interval)
{
    ESP_LOGI(TAG, "Setting Register Interval: %d", interval);
    m_StorageData.RegisterInterval = interval;
}

void Storage::SetEnabledSensors(Backend::Menus sensor, bool state)
{
    m_StorageData.EnabledSensors[sensor] = state;
}

void Storage::SetConfigMode(bool configBool)
{
    ESP_LOGI(TAG, "Setting Config Mode: %s", configBool == true ? "True" : "False");
    m_StorageData.ConfigMode = configBool;
}
