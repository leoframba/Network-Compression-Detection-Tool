#include "udp_train.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <netinet/ip.h>
#ifndef IP_MTU_DISCOVER
#include <linux/in.h>
#endif
#endif

static void sleep_microseconds(int usec)
{
    if (usec <= 0) {
        return;
    }
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (long)(usec % 1000000) * 1000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* retry remaining time */
    }
}

void udp_set_dont_fragment(int udp_sock)
{
#ifdef __linux__
#ifdef IP_PMTUDISC_DO
    int val = IP_PMTUDISC_DO;
    if (setsockopt(udp_sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
        perror("warning: IP_MTU_DISCOVER");
    }
#elif defined(IP_MTU_DISCOVER)
    int val = 1;
    if (setsockopt(udp_sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
        perror("warning: IP_MTU_DISCOVER");
    }
#endif
#else
    (void)udp_sock;
#endif
}

int send_udp_packet_train(long pack_count,
                          int payload_size,
                          unsigned char *payload,
                          int udp_sock,
                          struct sockaddr_in udp_addr,
                          int inter_packet_us,
                          int verbose)
{
    if (payload_size <= 2) {
        return -1;
    }

    socklen_t addr_size = sizeof(udp_addr);
    payload[0] = 0;
    payload[1] = 0;

    if (verbose) {
        printf("Sending UDP packet train (%ld packets, %d byte payload)...\n",
               pack_count, payload_size);
    }

    for (long i = 0; i < pack_count; i++) {
        ssize_t status = sendto(udp_sock, payload, (size_t)payload_size, 0,
                                (const struct sockaddr *)&udp_addr, addr_size);
        if (status <= 0) {
            return -1;
        }

        if (payload[1] == 255) {
            payload[0]++;
            payload[1] = 0;
        } else {
            payload[1]++;
        }

        if (inter_packet_us > 0) {
            sleep_microseconds(inter_packet_us);
        }
    }

    if (verbose) {
        printf("Finished sending UDP packet train.\n");
    }
    return 0;
}
