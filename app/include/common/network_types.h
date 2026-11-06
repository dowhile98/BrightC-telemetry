/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network common domain types, configurations, and pure validators.
 * Pure C99/C11 - no Zephyr headers, no vendor headers, no dynamic memory.
 */

#ifndef APP_COMMON_NETWORK_TYPES_H_
#define APP_COMMON_NETWORK_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * LIMITS AND CONSTANTS
 *============================================================================*/

#define NET_SSID_MAX_LEN 32U
#define NET_PSK_MAX_LEN 64U
#define NET_MAC_ADDR_LEN 6U

#define NET_WIFI_CHANNEL_MIN 1U
#define NET_WIFI_CHANNEL_MAX 14U

#define NET_WIFI_MAX_CLIENTS_DEFAULT 5U

/*============================================================================*
 * ENUMERATIONS
 *============================================================================*/

/**
 * @brief Logical network interfaces available on BrightC Controller V3.
 */
typedef enum {
    NETWORK_INTERFACE_ETHERNET = 0, /**< RMII wired ethernet interface */
    NETWORK_INTERFACE_WIFI,         /**< ESP-Hosted-MCU wireless interface */
    NETWORK_INTERFACE_MAX           /**< Sentinel: count of physical interfaces */
} network_interface_t;

/**
 * @brief Network interface connection states.
 */
typedef enum {
    NET_IF_STATE_DOWN = 0,     /**< Administratively disabled */
    NET_IF_STATE_DISCONNECTED, /**< Physical link down / not associated */
    NET_IF_STATE_CONNECTING,   /**< Negotiating link / DHCP / association */
    NET_IF_STATE_CONNECTED,    /**< Link up and IP bound */
    NET_IF_STATE_FAILED        /**< Fatal link or configuration failure */
} net_if_state_t;

/**
 * @brief Explicit finite state machine states for network connectivity adapters.
 */
typedef enum {
    NET_ADAPTER_STATE_UNINITIALIZED = 0,
    NET_ADAPTER_STATE_INIT,
    NET_ADAPTER_STATE_TRANSPORT_READY,
    NET_ADAPTER_STATE_LINK_UP,
    NET_ADAPTER_STATE_IP_ACQUIRED,
    NET_ADAPTER_STATE_DISCONNECTED,
    NET_ADAPTER_STATE_ERROR
} net_adapter_fsm_state_t;

/**
 * @brief IPv4 address configuration mode.
 */
typedef enum {
    NET_IP_MODE_DHCP = 0, /**< Dynamic host configuration */
    NET_IP_MODE_STATIC    /**< Static fixed IPv4 settings */
} net_ip_mode_t;

/**
 * @brief WiFi operating mode.
 */
typedef enum {
    NET_WIFI_MODE_STA = 0,   /**< Station mode (client) */
    NET_WIFI_MODE_AP = 1,    /**< Access Point mode (soft AP) */
    NET_WIFI_MODE_STA_AP = 2 /**< Concurrent Station + AP mode */
} net_wifi_mode_t;

/**
 * @brief WiFi security protocols.
 */
typedef enum {
    NET_WIFI_SEC_OPEN = 0,     /**< Unsecured / Open network */
    NET_WIFI_SEC_WPA_PSK = 2,  /**< WPA-PSK authentication */
    NET_WIFI_SEC_WPA2_PSK = 3, /**< WPA2-PSK (AES/CCMP) authentication */
    NET_WIFI_SEC_WPA3_SAE = 6  /**< WPA3-SAE authentication */
} net_wifi_security_t;

/**
 * @brief Network binding preference for high-level services (MQTT, HTTP, SNMP).
 */
typedef enum {
    NET_BIND_ANY = 0,          /**< Bind to any active interface */
    NET_BIND_ETHERNET_ONLY = 1,/**< Bind exclusively to wired Ethernet */
    NET_BIND_WIFI_ONLY = 2     /**< Bind exclusively to WiFi */
} net_service_bind_mode_t;

/**
 * @brief Domain result status code.
 */
typedef enum {
    NETWORK_RESULT_OK = 0,
    NETWORK_RESULT_INVALID = -1,
    NETWORK_RESULT_NOT_READY = -2,
    NETWORK_RESULT_NOT_CONNECTED = -3,
    NETWORK_RESULT_TIMEOUT = -4,
    NETWORK_RESULT_IO_ERROR = -5,
    NETWORK_RESULT_UNSUPPORTED = -6
} network_result_t;

/*============================================================================*
 * CONFIGURATION AND RUNTIME STRUCTURES (FLAT, PACKED FOR PERSISTENCE)
 *============================================================================*/

/**
 * @brief Flat Ethernet configuration structure suitable for EEPROM persistence.
 */
typedef struct __attribute__((packed)) {
    uint8_t  ip_mode;              /**< net_ip_mode_t (DHCP or STATIC) */
    uint8_t  mac_addr[NET_MAC_ADDR_LEN]; /**< Hardware MAC address (read-only) */
    uint8_t  auto_dhcpv4;          /**< 1: auto DHCP on link up; 0: manual */
    uint32_t ip_addr;              /**< IPv4 address in host order */
    uint32_t subnet_mask;          /**< Subnet mask in host order */
    uint32_t gateway;              /**< Gateway IPv4 in host order */
} net_eth_config_t;

/**
 * @brief WiFi Station parameters.
 */
typedef struct __attribute__((packed)) {
    char     ssid[NET_SSID_MAX_LEN + 1U]; /**< Target AP SSID (null-terminated) */
    char     psk[NET_PSK_MAX_LEN + 1U];   /**< Pre-shared key (null-terminated) */
    uint8_t  security;                   /**< net_wifi_security_t */
    uint8_t  ip_mode;                    /**< net_ip_mode_t (DHCP or STATIC) */
    uint16_t reserved;                   /**< Padding */
    uint32_t ip_addr;                    /**< Static IPv4 address in host order */
    uint32_t subnet_mask;                /**< Static subnet mask in host order */
    uint32_t gateway;                    /**< Static gateway in host order */
} net_wifi_sta_params_t;

/**
 * @brief WiFi Soft-AP parameters.
 */
typedef struct __attribute__((packed)) {
    char     ssid[NET_SSID_MAX_LEN + 1U]; /**< AP SSID (null-terminated) */
    char     psk[NET_PSK_MAX_LEN + 1U];   /**< AP PSK (null-terminated, empty if OPEN) */
    uint8_t  security;                   /**< net_wifi_security_t */
    uint8_t  channel;                    /**< Operating channel (1..14) */
    uint8_t  max_clients;                /**< Max associated stations */
    uint8_t  reserved;                   /**< Padding */
    uint32_t ap_ip_addr;                 /**< AP IPv4 gateway address */
    uint32_t ap_subnet_mask;             /**< AP subnet mask */
} net_wifi_ap_params_t;

/**
 * @brief Flat WiFi configuration structure suitable for EEPROM persistence.
 */
typedef struct __attribute__((packed)) {
    uint8_t               mode;                /**< net_wifi_mode_t (STA, AP, STA_AP) */
    uint8_t               mac_addr[NET_MAC_ADDR_LEN]; /**< Hardware STA MAC (read-only) */
    uint8_t               reserved;            /**< Padding */
    net_wifi_sta_params_t sta;                 /**< Parameters for STA mode */
    net_wifi_ap_params_t  ap;                  /**< Parameters for AP mode */
} net_wifi_config_t;

/**
 * @brief Runtime network status (ephemeral, not persisted).
 */
typedef struct {
    net_if_state_t state;       /**< Current interface state */
    uint32_t       ip_addr;     /**< Assigned IPv4 address in host order */
    uint32_t       subnet_mask; /**< Assigned subnet mask in host order */
    uint32_t       gateway;     /**< Assigned default gateway in host order */
    int8_t         rssi;        /**< WiFi RSSI in dBm (-128 if not applicable) */
    uint8_t        reserved[3]; /**< Alignment padding */
    uint32_t       link_up_ms;  /**< Milliseconds since link established */
} net_status_t;

/*============================================================================*
 * INLINED VALIDATORS (PURE PREDICATES)
 *============================================================================*/

/**
 * @brief Check if interface index is in valid range.
 */
static inline bool network_interface_is_valid(network_interface_t iface)
{
    return (unsigned int)iface < (unsigned int)NETWORK_INTERFACE_MAX;
}

/**
 * @brief Check if interface state is in valid range.
 */
static inline bool net_if_state_is_valid(net_if_state_t state)
{
    return (unsigned int)state <= (unsigned int)NET_IF_STATE_FAILED;
}

/**
 * @brief Check if network adapter FSM state is in valid range.
 */
static inline bool net_adapter_fsm_state_is_valid(net_adapter_fsm_state_t state)
{
    return (unsigned int)state <= (unsigned int)NET_ADAPTER_STATE_ERROR;
}

/**
 * @brief Check if IP mode is valid.
 */
static inline bool net_ip_mode_is_valid(net_ip_mode_t mode)
{
    return mode == NET_IP_MODE_DHCP || mode == NET_IP_MODE_STATIC;
}

/**
 * @brief Check if WiFi mode is valid.
 */
static inline bool net_wifi_mode_is_valid(net_wifi_mode_t mode)
{
    return mode == NET_WIFI_MODE_STA || mode == NET_WIFI_MODE_AP || mode == NET_WIFI_MODE_STA_AP;
}

/**
 * @brief Check if WiFi security enum is valid.
 */
static inline bool net_wifi_security_is_valid(net_wifi_security_t sec)
{
    return sec == NET_WIFI_SEC_OPEN || sec == NET_WIFI_SEC_WPA_PSK ||
           sec == NET_WIFI_SEC_WPA2_PSK || sec == NET_WIFI_SEC_WPA3_SAE;
}

/**
 * @brief Check if service binding mode enum is valid.
 */
static inline bool net_service_bind_mode_is_valid(net_service_bind_mode_t mode)
{
    return mode == NET_BIND_ANY || mode == NET_BIND_ETHERNET_ONLY || mode == NET_BIND_WIFI_ONLY;
}

/**
 * @brief Validate Ethernet configuration.
 */
static inline bool net_eth_config_is_valid(const net_eth_config_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (!net_ip_mode_is_valid((net_ip_mode_t)cfg->ip_mode)) {
        return false;
    }
    if ((net_ip_mode_t)cfg->ip_mode == NET_IP_MODE_STATIC) {
        if (cfg->ip_addr == 0U || cfg->subnet_mask == 0U) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Pure bounded string length helper (C99 compliant, no POSIX dependency).
 */
static inline size_t net_strnlen_helper(const char *str, size_t max_len)
{
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    return len;
}

/**
 * @brief Validate WiFi configuration.
 */
static inline bool net_wifi_config_is_valid(const net_wifi_config_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (!net_wifi_mode_is_valid((net_wifi_mode_t)cfg->mode)) {
        return false;
    }

    /* Validate STA settings if mode includes STA */
    if ((net_wifi_mode_t)cfg->mode == NET_WIFI_MODE_STA ||
        (net_wifi_mode_t)cfg->mode == NET_WIFI_MODE_STA_AP) {
        if (cfg->sta.ssid[0] == '\0') {
            return false;
        }
        if (!net_wifi_security_is_valid((net_wifi_security_t)cfg->sta.security)) {
            return false;
        }
        if ((net_wifi_security_t)cfg->sta.security != NET_WIFI_SEC_OPEN) {
            size_t psk_len = net_strnlen_helper(cfg->sta.psk, sizeof(cfg->sta.psk));
            if (psk_len < 8U || psk_len > NET_PSK_MAX_LEN) {
                return false;
            }
        }
        if (!net_ip_mode_is_valid((net_ip_mode_t)cfg->sta.ip_mode)) {
            return false;
        }
        if ((net_ip_mode_t)cfg->sta.ip_mode == NET_IP_MODE_STATIC) {
            if (cfg->sta.ip_addr == 0U || cfg->sta.subnet_mask == 0U) {
                return false;
            }
        }
    }

    /* Validate AP settings if mode includes AP */
    if ((net_wifi_mode_t)cfg->mode == NET_WIFI_MODE_AP ||
        (net_wifi_mode_t)cfg->mode == NET_WIFI_MODE_STA_AP) {
        if (cfg->ap.ssid[0] == '\0') {
            return false;
        }
        if (cfg->ap.channel < NET_WIFI_CHANNEL_MIN || cfg->ap.channel > NET_WIFI_CHANNEL_MAX) {
            return false;
        }
        if (!net_wifi_security_is_valid((net_wifi_security_t)cfg->ap.security)) {
            return false;
        }
        if ((net_wifi_security_t)cfg->ap.security != NET_WIFI_SEC_OPEN) {
            size_t psk_len = net_strnlen_helper(cfg->ap.psk, sizeof(cfg->ap.psk));
            if (psk_len < 8U || psk_len > NET_PSK_MAX_LEN) {
                return false;
            }
        }
    }

    return true;
}

/*============================================================================*
 * FACTORY DEFAULT CONFIGURATION GENERATORS
 *============================================================================*/

/**
 * @brief Populate default Ethernet configuration.
 *
 * DHCP enabled, auto DHCP enabled on link up, and static fallback parameters
 * (192.168.1.100 / 255.255.255.0 / 192.168.1.1).
 *
 * @param[out] cfg Pointer to structure to populate.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
static inline int network_default_eth_config(net_eth_config_t *cfg)
{
    if (cfg == NULL) {
        return -EINVAL;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->ip_mode = (uint8_t)NET_IP_MODE_DHCP;
    cfg->auto_dhcpv4 = 1U;
    cfg->ip_addr = 0xC0A80164U;     /* 192.168.1.100 */
    cfg->subnet_mask = 0xFFFFFF00U; /* 255.255.255.0 */
    cfg->gateway = 0xC0A80101U;     /* 192.168.1.1 */

    return 0;
}

/**
 * @brief Populate default WiFi configuration.
 *
 * Soft-AP mode, SSID "BrightC-AP", PSK "12345678", WPA2_PSK security,
 * channel 9, max 5 clients, AP IP 192.168.4.1 / 255.255.255.0.
 *
 * @param[out] cfg Pointer to structure to populate.
 * @return 0 on success, -EINVAL if cfg is NULL.
 */
static inline int network_default_wifi_config(net_wifi_config_t *cfg)
{
    if (cfg == NULL) {
        return -EINVAL;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = (uint8_t)NET_WIFI_MODE_AP;

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_SSID
#define CONFIG_BRIGHTC_WIFI_DEFAULT_SSID "BrightC-AP"
#endif

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_PSK
#define CONFIG_BRIGHTC_WIFI_DEFAULT_PSK "12345678"
#endif

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_IP
#define CONFIG_BRIGHTC_WIFI_DEFAULT_IP 0xC0A80401U /* 192.168.4.1 */
#endif

    /* Soft-AP parameters */
    strncpy(cfg->ap.ssid, CONFIG_BRIGHTC_WIFI_DEFAULT_SSID, sizeof(cfg->ap.ssid) - 1U);
    strncpy(cfg->ap.psk, CONFIG_BRIGHTC_WIFI_DEFAULT_PSK, sizeof(cfg->ap.psk) - 1U);
    cfg->ap.security = (uint8_t)NET_WIFI_SEC_WPA2_PSK;
    cfg->ap.channel = 9U;
    cfg->ap.max_clients = NET_WIFI_MAX_CLIENTS_DEFAULT;
    cfg->ap.ap_ip_addr = CONFIG_BRIGHTC_WIFI_DEFAULT_IP;
    cfg->ap.ap_subnet_mask = 0xFFFFFF00U; /* 255.255.255.0 */

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_NETWORK_TYPES_H_ */

