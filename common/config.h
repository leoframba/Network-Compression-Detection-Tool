#ifndef NCD_CONFIG_H
#define NCD_CONFIG_H

/**
 * Probe configuration shared by cooperative and standalone modes.
 * Field names match the JSON keys used in configs/example.json.
 */
typedef struct config_settings {
    char *server_ip;
    int source_port;
    int dest_port_udp;
    int dest_port_tcp_head;
    int dest_port_tcp_tail;
    int tcp_port;
    int udp_payload_size;
    int inter_time_sec;
    long udp_pack_count;
    int ttl;
    int threshold_ms;           /* ΔtH - ΔtL detection threshold (paper: ~100 ms) */
    int inter_packet_us;        /* spacing between UDP packets (paper: ~100 µs) */
    char *interface;            /* NIC name for standalone mode (optional) */
    int trials;                 /* number of measurement trials */
    double data_threshold;      /* fraction of train before timing starts (0 = first packet) */
} config_settings;

/**
 * Parse a JSON config file. Returns a heap-allocated settings struct.
 * Caller must free with config_free(). Exits on unrecoverable errors.
 */
config_settings *config_parse(const char *path);

/** Release memory owned by a config_settings struct. */
void config_free(config_settings *settings);

/** Apply CLI overrides; negative / NULL values mean "leave unchanged". */
void config_apply_overrides(config_settings *settings,
                            int threshold_ms,
                            int trials,
                            const char *interface);

#endif /* NCD_CONFIG_H */
