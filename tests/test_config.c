#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    } else {
        printf("PASS: %s\n", msg);
    }
}

int main(void)
{
    const char *path = "configs/example.json";
    config_settings *settings = config_parse(path);

    expect_true(settings != NULL, "config_parse returns settings");
    expect_true(strcmp(settings->server_ip, "127.0.0.1") == 0, "serverIp parsed");
    expect_true(settings->udp_payload_size == 1100, "UdpPayloadSize paper default");
    expect_true(settings->udp_pack_count == 6000, "UdpPackCount paper default");
    expect_true(settings->threshold_ms == 100, "ThresholdMs paper default");
    expect_true(settings->inter_packet_us == 100, "InterPacketUs paper default");
    expect_true(settings->tcp_port == 8100, "TcpPortNum parsed");
    expect_true(settings->trials == 1, "Trials default");

    config_apply_overrides(settings, 250, 3, "eth0");
    expect_true(settings->threshold_ms == 250, "threshold override");
    expect_true(settings->trials == 3, "trials override");
    expect_true(settings->interface && strcmp(settings->interface, "eth0") == 0,
                "interface override");

    config_free(settings);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
