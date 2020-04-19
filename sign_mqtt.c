#include <stdio.h>
#include <string.h>

#include "sign_api.h"

/* #define SUPPORT_TLS */

/* all secure mode define */
#define MODE_TLS_GUIDER             "-1"
#define MODE_TLS_DIRECT             "2"
#define MODE_TCP_DIRECT_PLAIN       "3"
#define MODE_ITLS_DNS_ID2           "8"

#if defined(MQTT_PRE_AUTH)
    #define SECURE_MODE             MODE_TLS_GUIDER
#elif defined(SUPPORT_ITLS)
    #define SECURE_MODE             MODE_ITLS_DNS_ID2
#elif defined(SUPPORT_TLS)
    #define SECURE_MODE             MODE_TLS_DIRECT
#else
    #define SECURE_MODE             MODE_TCP_DIRECT_PLAIN
#endif

/* use fixed timestamp */
#define TIMESTAMP_VALUE             "2524608000000"

extern void utils_hmac_sha256(const uint8_t *msg, uint32_t msg_len, const uint8_t *key, uint32_t key_len, uint8_t output[32]);

#define IOTX_MQTT_DOMAIN_NUMBER (6)
const char * g_infra_mqtt_domain[IOTX_MQTT_DOMAIN_NUMBER] = {
    "iot-as-mqtt.cn-shanghai.aliyuncs.com",       /* Shanghai */
    "iot-as-mqtt.ap-southeast-1.aliyuncs.com",    /* Singapore */
    "iot-as-mqtt.ap-northeast-1.aliyuncs.com",    /* Japan */
    "iot-as-mqtt.us-west-1.aliyuncs.com",         /* America */
    "iot-as-mqtt.eu-central-1.aliyuncs.com",      /* Germany */
    NULL,                                           /* Custom */
};

/* clientid key value pair define */
const char *clientid_kv[][2] = {
    {
        "timestamp", TIMESTAMP_VALUE
    },
    {
        "_v", "sdk-c-3.0.1.1"
    },
    {
        "securemode", SECURE_MODE
    },
    {
        "signmethod", "hmacsha256"
    },
    {
        "lan", "C"
    },
#if 0
    {
        "v", IOTX_ALINK_VERSION
    },
#else
    {
        "gw", "0"
    },
    {
        "ext", "0"
    },
#endif
#ifdef SUPPORT_ITLS
    {
        "authtype", "id2"
    }
#endif
};

static void _hex2str(uint8_t *input, uint16_t input_len, char *output)
{
    char *zEncode = "0123456789ABCDEF";
    int i = 0, j = 0;

    for (i = 0; i < input_len; i++) {
        output[j++] = zEncode[(input[i] >> 4) & 0xf];
        output[j++] = zEncode[(input[i]) & 0xf];
    }
}

int _sign_get_clientid(char *clientid_string, const char *device_id, const char *custom_kv)
{
    uint8_t i;

    if (clientid_string == NULL || device_id == NULL) {
        return -1;
    }

    memset(clientid_string, 0, DEV_SIGN_CLIENT_ID_MAXLEN);
    memcpy(clientid_string, device_id, strlen(device_id));
    memcpy(clientid_string + strlen(clientid_string), "|", 1);

    for (i = 0; i < (sizeof(clientid_kv) / (sizeof(clientid_kv[0]))); i++) {
        if ((strlen(clientid_string) + strlen(clientid_kv[i][0]) + strlen(clientid_kv[i][1]) + 2) >=
            DEV_SIGN_CLIENT_ID_MAXLEN) {
            return -1;
        }

        memcpy(clientid_string + strlen(clientid_string), clientid_kv[i][0], strlen(clientid_kv[i][0]));
        memcpy(clientid_string + strlen(clientid_string), "=", 1);
        memcpy(clientid_string + strlen(clientid_string), clientid_kv[i][1], strlen(clientid_kv[i][1]));
        memcpy(clientid_string + strlen(clientid_string), ",", 1);
    }

    if (custom_kv != NULL) {
        if ((strlen(clientid_string) + strlen(custom_kv) + 1) >= DEV_SIGN_CLIENT_ID_MAXLEN) {
            return -1;
        }
        memcpy(clientid_string + strlen(clientid_string), custom_kv, strlen(custom_kv));
        memcpy(clientid_string + strlen(clientid_string), ",", 1);
    }

    memcpy(clientid_string + strlen(clientid_string) - 1, "|", 1);

    return 0;
}

int _iotx_generate_sign_string(const char *device_id, const char *device_name, const char *product_key,
                               const char *device_secret, char *sign_string)
{
    char signsource[DEV_SIGN_SOURCE_MAXLEN] = {0};
    uint16_t signsource_len = 0;
    const char sign_fmt[] = "clientId%sdeviceName%sproductKey%stimestamp%s";
    uint8_t sign_hex[32] = {0};

    signsource_len = sizeof(sign_fmt) + strlen(device_id) + strlen(device_name) + strlen(product_key) + strlen(
                                 TIMESTAMP_VALUE);
    if (signsource_len >= DEV_SIGN_SOURCE_MAXLEN) {
        return -1;
    }

    memset(signsource, 0, DEV_SIGN_SOURCE_MAXLEN);
    memcpy(signsource, "clientId", strlen("clientId"));
    memcpy(signsource + strlen(signsource), device_id, strlen(device_id));
    memcpy(signsource + strlen(signsource), "deviceName", strlen("deviceName"));
    memcpy(signsource + strlen(signsource), device_name, strlen(device_name));
    memcpy(signsource + strlen(signsource), "productKey", strlen("productKey"));
    memcpy(signsource + strlen(signsource), product_key, strlen(product_key));
    memcpy(signsource + strlen(signsource), "timestamp", strlen("timestamp"));
    memcpy(signsource + strlen(signsource), TIMESTAMP_VALUE, strlen(TIMESTAMP_VALUE));

    utils_hmac_sha256((uint8_t *)signsource, strlen(signsource), (uint8_t *)device_secret,
                      strlen(device_secret), sign_hex);

    _hex2str(sign_hex, 32, sign_string);

    return 0;
}

int32_t IOT_Sign_MQTT(iotx_mqtt_region_types_t region, iotx_dev_meta_info_t *meta, iotx_sign_mqtt_t *signout)
{
    uint16_t length = 0;
    char device_id[IOTX_PRODUCT_KEY_LEN + IOTX_DEVICE_NAME_LEN + 1] = {0};
    int res;

    if (region >= IOTX_MQTT_DOMAIN_NUMBER || meta == NULL) {
        return -1;
    }

    memset(signout, 0, sizeof(iotx_sign_mqtt_t));

    memcpy(device_id, meta->product_key, strlen(meta->product_key));
    memcpy(device_id + strlen(device_id), ".", strlen("."));
    memcpy(device_id + strlen(device_id), meta->device_name, strlen(meta->device_name));

    /* setup clientid */
    if (_sign_get_clientid(signout->clientid, device_id, NULL) != 0) {
        return -1;
    }

    /* setup password */
    memset(signout->password, 0, DEV_SIGN_PASSWORD_MAXLEN);
    res = _iotx_generate_sign_string(device_id, meta->device_name, meta->product_key, meta->device_secret,
                                     signout->password);
    if (res < 0) {
        return res;
    }

    /* setup hostname */
    if (IOTX_CLOUD_REGION_CUSTOM == region) {
        if (g_infra_mqtt_domain[region] == NULL) {
            return -1;
        }

        length = strlen(g_infra_mqtt_domain[region]) + 1;
        if (length >= DEV_SIGN_HOSTNAME_MAXLEN) {
            return -1;
        }

        memset(signout->hostname, 0, DEV_SIGN_HOSTNAME_MAXLEN);
        memcpy(signout->hostname, g_infra_mqtt_domain[region], strlen(g_infra_mqtt_domain[region]));
    } else {
        length = strlen(meta->product_key) + strlen(g_infra_mqtt_domain[region]) + 2;
        if (length >= DEV_SIGN_HOSTNAME_MAXLEN) {
            return -1;
        }
        memset(signout->hostname, 0, DEV_SIGN_HOSTNAME_MAXLEN);
        memcpy(signout->hostname, meta->product_key, strlen(meta->product_key));
        memcpy(signout->hostname + strlen(signout->hostname), ".", strlen("."));
        memcpy(signout->hostname + strlen(signout->hostname), g_infra_mqtt_domain[region],
               strlen(g_infra_mqtt_domain[region]));
    }

    /* setup username */
    length = strlen(meta->device_name) + strlen(meta->product_key) + 2;
    if (length >= DEV_SIGN_USERNAME_MAXLEN) {
        return -1;
    }
    memset(signout->username, 0, DEV_SIGN_USERNAME_MAXLEN);
    memcpy(signout->username, meta->device_name, strlen(meta->device_name));
    memcpy(signout->username + strlen(signout->username), "&", strlen("&"));
    memcpy(signout->username + strlen(signout->username), meta->product_key, strlen(meta->product_key));

    /* setup port */
#ifdef SUPPORT_TLS
    signout->port = 443;
#else
    signout->port = 1883;
#endif /* #ifdef SUPPORT_TLS */
    return 0;
}
