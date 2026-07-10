#include "config.h"
#include "util.h"

#include "vendor/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fatalf("Unable to open config file: %s", path);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fatal("fseek");
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        fatal("ftell");
    }
    rewind(file);

    char *buffer = xmalloc((size_t)file_size + 1);
    size_t nread = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    buffer[nread] = '\0';
    return buffer;
}

static cJSON *require_item(cJSON *json, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item) {
        fatalf("Config missing required key: %s", key);
    }
    return item;
}

static int optional_int(cJSON *json, const char *key, int default_value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item || !cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valueint;
}

static double optional_double(cJSON *json, const char *key, double default_value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item || !cJSON_IsNumber(item)) {
        return default_value;
    }
    return item->valuedouble;
}

static char *optional_string(cJSON *json, const char *key, const char *default_value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!item || !cJSON_IsString(item) || item->valuestring == NULL) {
        return default_value ? xstrdup(default_value) : NULL;
    }
    return xstrdup(item->valuestring);
}

static void validate(config_settings *settings)
{
    if (!settings->server_ip || settings->server_ip[0] == '\0') {
        fatal("Config serverIp must be a non-empty string");
    }
    if (settings->udp_payload_size <= 2) {
        fatal("UdpPayloadSize must be > 2 (first two bytes store packet id)");
    }
    if (settings->udp_pack_count <= 0) {
        fatal("UdpPackCount must be > 0");
    }
    if (settings->inter_time_sec < 1) {
        fatal("InterTime must be >= 1");
    }
    if (settings->threshold_ms < 0) {
        fatal("ThresholdMs must be >= 0");
    }
    if (settings->inter_packet_us < 0) {
        fatal("InterPacketUs must be >= 0");
    }
    if (settings->trials < 1) {
        fatal("Trials must be >= 1");
    }
    if (settings->data_threshold < 0.0 || settings->data_threshold >= 1.0) {
        fatal("DataThreshold must be in [0.0, 1.0)");
    }
    if (settings->tcp_port <= 0 || settings->tcp_port > 65535) {
        fatal("TcpPortNum must be a valid port");
    }
    if (settings->dest_port_udp <= 0 || settings->dest_port_udp > 65535) {
        fatal("destPortUdp must be a valid port");
    }
}

config_settings *config_parse(const char *path)
{
    if (!path) {
        fatal("Invalid config file path");
    }

    char *text = read_file(path);
    cJSON *json = cJSON_Parse(text);
    free(text);
    if (!json) {
        const char *err = cJSON_GetErrorPtr();
        fatalf("Failed to parse JSON config%s%s",
               err ? " near: " : "",
               err ? err : "");
    }

    config_settings *settings = xmalloc(sizeof(*settings));

    cJSON *server_ip = require_item(json, "serverIp");
    if (!cJSON_IsString(server_ip) || !server_ip->valuestring) {
        cJSON_Delete(json);
        fatal("serverIp must be a string");
    }
    settings->server_ip = xstrdup(server_ip->valuestring);

    settings->source_port = optional_int(json, "sourcePort", 9876);
    settings->dest_port_udp = optional_int(json, "destPortUdp", 8765);
    settings->dest_port_tcp_head = optional_int(json, "destPortTcpHead", 8000);
    settings->dest_port_tcp_tail = optional_int(json, "destPortTcpTail", 8080);
    settings->tcp_port = optional_int(json, "TcpPortNum", 8100);
    settings->udp_payload_size = optional_int(json, "UdpPayloadSize", 1100);
    settings->inter_time_sec = optional_int(json, "InterTime", 15);
    settings->udp_pack_count = optional_int(json, "UdpPackCount", 6000);
    settings->ttl = optional_int(json, "Ttl", 255);
    settings->threshold_ms = optional_int(json, "ThresholdMs", 100);
    settings->inter_packet_us = optional_int(json, "InterPacketUs", 100);
    settings->trials = optional_int(json, "Trials", 1);
    settings->data_threshold = optional_double(json, "DataThreshold", 0.0);
    settings->interface = optional_string(json, "Interface", NULL);

    cJSON_Delete(json);
    validate(settings);
    return settings;
}

void config_free(config_settings *settings)
{
    if (!settings) {
        return;
    }
    free(settings->server_ip);
    free(settings->interface);
    free(settings);
}

void config_apply_overrides(config_settings *settings,
                            int threshold_ms,
                            int trials,
                            const char *interface)
{
    if (threshold_ms >= 0) {
        settings->threshold_ms = threshold_ms;
    }
    if (trials > 0) {
        settings->trials = trials;
    }
    if (interface && interface[0] != '\0') {
        free(settings->interface);
        settings->interface = xstrdup(interface);
    }
}
