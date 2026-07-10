#ifndef NCD_UDP_TRAIN_H
#define NCD_UDP_TRAIN_H

#include <netinet/in.h>

/**
 * Send a UDP packet train. Payload first two bytes are used as a 16-bit
 * sequence id. Returns 0 on success, -1 on failure.
 */
int send_udp_packet_train(long pack_count,
                          int payload_size,
                          unsigned char *payload,
                          int udp_sock,
                          struct sockaddr_in udp_addr,
                          int inter_packet_us,
                          int verbose);

/**
 * Optionally set don't-fragment / path-MTU discovery on a UDP socket.
 * No-op on platforms that lack the option.
 */
void udp_set_dont_fragment(int udp_sock);

#endif /* NCD_UDP_TRAIN_H */
