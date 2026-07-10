#include "cli.h"
#include "config.h"
#include "timing.h"
#include "util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define RECEIVED_CONFIG_PATH "received_config.json"

typedef struct packet_probe {
    long time_ms;
    int last_packet_recv;
    int packets_recv;
} packet_probe;

static void recv_config_file(int com_sock, const char *out_path)
{
    FILE *file = fopen(out_path, "wb");
    if (!file) {
        fatal("Error opening config file for write");
    }

    char buffer[4096];
    for (;;) {
        ssize_t n = recv(com_sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        if (fwrite(buffer, 1, (size_t)n, file) != (size_t)n) {
            fclose(file);
            fatal("Error writing received config");
        }
    }

    fclose(file);
    printf("Received config file.\n");
    fflush(stdout);
}

static int create_listen_socket(int port)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        fatal("Error opening TCP socket");
    }

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fatal("Error binding TCP socket");
    }
    if (listen(listen_sock, 4) < 0) {
        fatal("Error listening for TCP client");
    }
    return listen_sock;
}

static int accept_client(int listen_sock, int verbose)
{
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(addr);
    int com_sock = accept(listen_sock, (struct sockaddr *)&addr, &addr_size);
    if (com_sock < 0) {
        fatal("Error accepting client connection");
    }
    if (verbose) {
        printf("Accepted client connection.\n");
        fflush(stdout);
    }
    return com_sock;
}

static int get_udp_packet_train(long pack_count, int payload_size, int sock,
                                double data_threshold, packet_probe *probe,
                                int verbose)
{
    if (verbose) {
        printf("Receiving packet train...\n");
        fflush(stdout);
    }

    probe->time_ms = -1;
    probe->packets_recv = 0;
    probe->last_packet_recv = -1;

    unsigned char *buffer = xmalloc((size_t)payload_size);
    struct timespec start, end;
    int have_end = 0;
    int curr_packet_id = -1;
    int status = 0;
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);

    if (recvfrom(sock, buffer, (size_t)payload_size, 0,
                 (struct sockaddr *)&peer, &peer_len) < 0) {
        if (verbose) {
            printf("Timeout waiting for first packet.\n");
            fflush(stdout);
        }
        free(buffer);
        return -1;
    }
    curr_packet_id = buffer[1] + (buffer[0] << 8);
    probe->packets_recv = 1;
    clock_gettime(CLOCK_MONOTONIC, &start);
    end = start;

    int threshold_index = (int)(pack_count * data_threshold);

    for (long i = 1; i < pack_count; i++) {
        peer_len = sizeof(peer);
        if (recvfrom(sock, buffer, (size_t)payload_size, 0,
                     (struct sockaddr *)&peer, &peer_len) < 0) {
            if (verbose) {
                printf("Timeout at packet id %d (received %d).\n",
                       curr_packet_id, probe->packets_recv);
                fflush(stdout);
            }
            status = -1;
            break;
        }
        probe->packets_recv++;
        curr_packet_id = buffer[1] + (buffer[0] << 8);

        if (i >= threshold_index) {
            clock_gettime(CLOCK_MONOTONIC, &end);
            have_end = 1;
        }
    }

    probe->last_packet_recv = curr_packet_id;
    if (have_end || probe->packets_recv > 1) {
        if (!have_end) {
            clock_gettime(CLOCK_MONOTONIC, &end);
        }
        probe->time_ms = timespec_to_ms(timespec_delta(start, end));
    }

    free(buffer);
    if (verbose) {
        printf("Packet train complete (%d packets, %ld ms).\n",
               probe->packets_recv, probe->time_ms);
        fflush(stdout);
    }
    return status;
}

static int run_one_session(int listen_sock, int verbose)
{
    if (verbose) {
        printf("\n********** Pre-Probing Phase **********\n");
        fflush(stdout);
    }

    int com_sock = accept_client(listen_sock, verbose);
    recv_config_file(com_sock, RECEIVED_CONFIG_PATH);
    close(com_sock);

    config_settings *settings = config_parse(RECEIVED_CONFIG_PATH);

    if (verbose) {
        printf("\n********** Probing Phase **********\n");
        fflush(stdout);
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        fatal("Failed to open UDP socket");
    }

    int reuse = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Cover client InterTime gap between trains plus margin. */
    int wait_sec = settings->inter_time_sec + 10;
    struct timeval timeout = {.tv_sec = wait_sec, .tv_usec = 0};
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)settings->dest_port_udp);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fatal("Failed to bind UDP socket");
    }
    if (verbose) {
        printf("Bound UDP socket on port %d.\n", settings->dest_port_udp);
        fflush(stdout);
    }

    packet_probe low = {0};
    packet_probe high = {0};

    get_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                         udp_sock, settings->data_threshold, &low, verbose);

    get_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                         udp_sock, settings->data_threshold, &high, verbose);
    close(udp_sock);

    if (verbose) {
        printf("Low:  %ld ms (packets=%d)\n", low.time_ms, low.packets_recv);
        printf("High: %ld ms (packets=%d)\n", high.time_ms, high.packets_recv);
        printf("Delta: %ld ms\n", high.time_ms - low.time_ms);
        fflush(stdout);
    }

    if (verbose) {
        printf("\n********** Post-Probing Phase **********\n");
        fflush(stdout);
    }

    com_sock = accept_client(listen_sock, verbose);

    long results[3];
    results[0] = low.time_ms;
    results[1] = high.time_ms;
    results[2] = (high.time_ms < 0 || low.time_ms < 0) ? 1 : 0;

    if (send(com_sock, results, sizeof(results), 0) < (ssize_t)sizeof(results)) {
        fatal("Failed to send results to client");
    }
    close(com_sock);

    print_detection_result(low.time_ms, high.time_ms, settings->threshold_ms,
                           results[2] == 0, 0);
    fflush(stdout);

    config_free(settings);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    cli_options opts;
    if (cli_parse(argc, argv, &opts, 1) != 0) {
        cli_print_usage(argv[0], 1, 0);
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        cli_print_usage(argv[0], 1, 0);
        return EXIT_SUCCESS;
    }

    int listen_port = 8100;
    if (opts.config_path) {
        config_settings *boot = config_parse(opts.config_path);
        listen_port = boot->tcp_port;
        config_free(boot);
    }
    if (opts.listen_port > 0) {
        listen_port = opts.listen_port;
    }

    int listen_sock = create_listen_socket(listen_port);
    printf("ncd-server listening for cooperative probes on TCP port %d\n", listen_port);
    printf("Press Ctrl+C to stop. Each client session runs one full probe.\n");
    fflush(stdout);

    for (;;) {
        run_one_session(listen_sock, opts.verbose || 1);
    }

    close(listen_sock);
    return EXIT_SUCCESS;
}
