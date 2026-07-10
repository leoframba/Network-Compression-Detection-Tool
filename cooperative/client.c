#include "cli.h"
#include "config.h"
#include "entropy.h"
#include "timing.h"
#include "udp_train.h"
#include "util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void send_config_file(int sock, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fatalf("Failed to open config file: %s", path);
    }

    char buffer[4096];
    size_t nread;
    while ((nread = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        size_t sent = 0;
        while (sent < nread) {
            ssize_t n = send(sock, buffer + sent, nread - sent, 0);
            if (n < 0) {
                fclose(file);
                fatal("Error sending config file");
            }
            sent += (size_t)n;
        }
    }
    fclose(file);
}

static int init_tcp_sock(int port, in_addr_t server_ip, int verbose, int retries)
{
    for (int attempt = 0; attempt <= retries; attempt++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            fatal("Error creating TCP socket");
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = server_ip;

        if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) == 0) {
            if (verbose) {
                char ipstr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr.sin_addr, ipstr, sizeof(ipstr));
                printf("Connected to server %s:%d\n", ipstr, port);
            }
            return sock;
        }

        close(sock);
        if (attempt < retries) {
            if (verbose) {
                printf("Waiting for server (retry %d/%d)...\n", attempt + 1, retries);
            }
            sleep(1);
        }
    }

    fatal("Error connecting to server");
    return -1;
}

static int run_one_trial(config_settings *settings, const char *config_path,
                         int verbose, long *out_low, long *out_high, int *out_ok)
{
    struct in_addr addr;
    if (inet_pton(AF_INET, settings->server_ip, &addr) != 1) {
        fatalf("Invalid serverIp: %s", settings->server_ip);
    }
    in_addr_t server_ip = addr.s_addr;

    if (verbose) {
        printf("\n********** Pre-Probing Phase **********\n");
    }

    int com_sock = init_tcp_sock(settings->tcp_port, server_ip, verbose, 10);
    send_config_file(com_sock, config_path);
    if (verbose) {
        printf("Config file sent.\n");
    }
    close(com_sock);

    sleep(2);

    if (verbose) {
        printf("\n********** Probing Phase **********\n");
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        fatal("Error creating UDP socket");
    }
    udp_set_dont_fragment(udp_sock);

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons((uint16_t)settings->dest_port_udp);
    udp_addr.sin_addr.s_addr = server_ip;

    unsigned char *buffer = xmalloc((size_t)settings->udp_payload_size);

    fill_low_entropy(buffer, (size_t)settings->udp_payload_size);
    if (send_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                              buffer, udp_sock, udp_addr,
                              settings->inter_packet_us, verbose) < 0) {
        free(buffer);
        close(udp_sock);
        fatal("Failed to send low-entropy packet train");
    }

    if (verbose) {
        printf("Waiting intermission (%d s)...\n", settings->inter_time_sec);
    }
    sleep((unsigned int)settings->inter_time_sec);

    fill_high_entropy(buffer, (size_t)settings->udp_payload_size);
    if (send_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                              buffer, udp_sock, udp_addr,
                              settings->inter_packet_us, verbose) < 0) {
        free(buffer);
        close(udp_sock);
        fatal("Failed to send high-entropy packet train");
    }

    free(buffer);
    close(udp_sock);

    /* Allow server to finish / time out before post-probing. */
    sleep((unsigned int)settings->inter_time_sec);

    if (verbose) {
        printf("\n********** Post-Probing Phase **********\n");
    }

    com_sock = init_tcp_sock(settings->tcp_port, server_ip, verbose, 30);
    long results[3] = {0, 0, 1};
    ssize_t n = recv(com_sock, results, sizeof(results), MSG_WAITALL);
    close(com_sock);

    if (n != (ssize_t)sizeof(results)) {
        fprintf(stderr, "Failed to receive complete results from server.\n");
        *out_ok = 0;
        return -1;
    }

    *out_low = results[0];
    *out_high = results[1];
    *out_ok = (results[2] == 0);
    return 0;
}

int main(int argc, char **argv)
{
    cli_options opts;
    if (cli_parse(argc, argv, &opts, 0) != 0) {
        cli_print_usage(argv[0], 0, 0);
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        cli_print_usage(argv[0], 0, 0);
        return EXIT_SUCCESS;
    }

    config_settings *settings = config_parse(opts.config_path);
    config_apply_overrides(settings, opts.threshold_ms, opts.trials, opts.interface);

    int detections = 0;
    int valid_trials = 0;
    long sum_low = 0;
    long sum_high = 0;

    for (int t = 0; t < settings->trials; t++) {
        if (settings->trials > 1 && !opts.json_mode) {
            printf("\n=== Trial %d / %d ===\n", t + 1, settings->trials);
        }

        long low = 0;
        long high = 0;
        int ok = 0;
        if (run_one_trial(settings, opts.config_path, opts.verbose, &low, &high, &ok) != 0) {
            continue;
        }

        if (!opts.json_mode || settings->trials == 1) {
            print_detection_result(low, high, settings->threshold_ms, ok, opts.json_mode);
        }

        if (ok) {
            valid_trials++;
            sum_low += low;
            sum_high += high;
            if (compression_detected(low, high, settings->threshold_ms)) {
                detections++;
            }
        }
    }

    if (settings->trials > 1) {
        if (opts.json_mode) {
            printf("{\"trials\":%d,\"valid_trials\":%d,\"detections\":%d,"
                   "\"avg_low_ms\":%.2f,\"avg_high_ms\":%.2f,\"threshold_ms\":%d,"
                   "\"compression_majority\":%s}\n",
                   settings->trials, valid_trials, detections,
                   valid_trials ? (double)sum_low / valid_trials : 0.0,
                   valid_trials ? (double)sum_high / valid_trials : 0.0,
                   settings->threshold_ms,
                   (detections * 2 > valid_trials) ? "true" : "false");
        } else {
            printf("\n=== Summary ===\n");
            printf("Trials: %d  Valid: %d  Detections: %d\n",
                   settings->trials, valid_trials, detections);
            if (valid_trials > 0) {
                printf("Average low:  %.1f ms\n", (double)sum_low / valid_trials);
                printf("Average high: %.1f ms\n", (double)sum_high / valid_trials);
                printf("Majority verdict: %s\n",
                       (detections * 2 > valid_trials)
                           ? "Compression detected"
                           : "No compression detected");
            }
        }
    }

    config_free(settings);
    return EXIT_SUCCESS;
}
