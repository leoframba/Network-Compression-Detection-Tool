/*
 * Standalone (responsive-host) compression probe.
 *
 * Instead of the paper's ICMP head/tail markers, this implementation sends
 * raw TCP SYN packets and times the RST replies via libpcap. Requires root
 * (or CAP_NET_RAW) and is primarily intended for Linux.
 *
 * Raw socket construction adapted from P.D. Buchan's examples:
 *   https://www.pdbuchan.com/rawsock/rawsock.html
 * libpcap usage adapted from DevDungeon:
 *   https://www.devdungeon.com/content/using-libpcap-c
 */

#ifndef __FAVOR_BSD
#define __FAVOR_BSD
#endif

#include "cli.h"
#include "config.h"
#include "entropy.h"
#include "timing.h"
#include "udp_train.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <net/ethernet.h>
#elif defined(__linux__)
#include <netinet/if_ether.h>
#else
#include <netinet/if_ether.h>
#endif

#ifndef ETHERTYPE_IP
#define ETHERTYPE_IP 0x0800
#endif

#define IP4_HDRLEN 20
#define TCP_HDRLEN 20

#ifndef IP_MAXPACKET
#define IP_MAXPACKET 65535
#endif

#ifndef TH_SYN
#define TH_SYN 0x02
#endif

static int g_rst_count = 0;
static struct timeval g_times[4];
static pcap_t *g_pcap = NULL;

static uint16_t ip_checksum(uint16_t *addr, int len);
static uint16_t tcp4_checksum(struct ip iphdr, struct tcphdr tcphdr);

static void alarm_handler(int sig)
{
    (void)sig;
    if (g_pcap) {
        pcap_breakloop(g_pcap);
    }
}

static void packet_handler(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
    (void)args;

    const struct ether_header *eth = (const struct ether_header *)packet;
    if (ntohs(eth->ether_type) != ETHERTYPE_IP) {
        return;
    }

    const u_char *ip_header = packet + 14;
    int ip_header_length = ((*ip_header) & 0x0F) * 4;
    if (*(ip_header + 9) != IPPROTO_TCP) {
        return;
    }

    const u_char *tcp_header = packet + 14 + ip_header_length;
    unsigned char flags = *(tcp_header + 13);
    if ((flags & 0x04) == 0) { /* RST */
        return;
    }

    if (g_rst_count < 4) {
        g_times[g_rst_count] = header->ts;
        g_rst_count++;
        if (g_rst_count == 4 && g_pcap) {
            pcap_breakloop(g_pcap);
        }
    }
}

static int resolve_interface_ip(const char *interface, char *src_ip, size_t src_ip_len, int *ifindex)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interface);

#ifdef SIOCGIFINDEX
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }
    *ifindex = (int)ifr.ifr_ifindex;
#else
    *ifindex = (int)if_nametoindex(interface);
    if (*ifindex == 0) {
        close(fd);
        return -1;
    }
#endif

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    if (!inet_ntop(AF_INET, &addr->sin_addr, src_ip, (socklen_t)src_ip_len)) {
        return -1;
    }
    return 0;
}

static char *default_pcap_device(char *errbuf)
{
    pcap_if_t *alldevs = NULL;
    if (pcap_findalldevs(&alldevs, errbuf) == -1 || !alldevs) {
        return NULL;
    }
    char *name = xstrdup(alldevs->name);
    pcap_freealldevs(alldevs);
    return name;
}

static void rebuild_syn_packet(uint8_t *packet, struct ip *iphdr, struct tcphdr *tcphdr,
                               uint16_t dest_port)
{
    tcphdr->th_dport = htons(dest_port);
    tcphdr->th_sum = 0;
    tcphdr->th_sum = tcp4_checksum(*iphdr, *tcphdr);
    memcpy(packet, iphdr, IP4_HDRLEN);
    memcpy(packet + IP4_HDRLEN, tcphdr, TCP_HDRLEN);
}

static int send_syn(int sd, uint8_t *packet, struct sockaddr_in *sin)
{
    if (sendto(sd, packet, IP4_HDRLEN + TCP_HDRLEN, 0,
               (struct sockaddr *)sin, sizeof(*sin)) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    cli_options opts;
    if (cli_parse(argc, argv, &opts, 0) != 0) {
        cli_print_usage(argv[0], 0, 1);
        return EXIT_FAILURE;
    }
    if (opts.show_help) {
        cli_print_usage(argv[0], 0, 1);
        return EXIT_SUCCESS;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Warning: standalone probe usually requires root "
                        "(raw sockets + packet capture).\n");
    }

    config_settings *settings = config_parse(opts.config_path);
    config_apply_overrides(settings, opts.threshold_ms, opts.trials, opts.interface);

    char interface[IFNAMSIZ];
    if (settings->interface && settings->interface[0]) {
        snprintf(interface, sizeof(interface), "%s", settings->interface);
    } else {
        char errbuf[PCAP_ERRBUF_SIZE];
        char *auto_dev = default_pcap_device(errbuf);
        if (!auto_dev) {
            fatalf("No interface configured and auto-detect failed: %s\n"
                   "Pass --interface <name> or set Interface in the config.",
                   errbuf);
        }
        snprintf(interface, sizeof(interface), "%s", auto_dev);
        free(auto_dev);
        if (opts.verbose) {
            printf("Auto-selected interface: %s\n", interface);
        }
    }

    char src_ip[INET_ADDRSTRLEN];
    int ifindex = 0;
    if (resolve_interface_ip(interface, src_ip, sizeof(src_ip), &ifindex) != 0) {
        fatalf("Failed to resolve address for interface %s", interface);
    }
    if (opts.verbose) {
        printf("Interface %s (index %d) src IP %s\n", interface, ifindex, src_ip);
    }

    char dst_ip[INET_ADDRSTRLEN];
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int status = getaddrinfo(settings->server_ip, NULL, &hints, &res);
    if (status != 0) {
        fatalf("getaddrinfo failed for %s: %s", settings->server_ip, gai_strerror(status));
    }
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    if (!inet_ntop(AF_INET, &ipv4->sin_addr, dst_ip, sizeof(dst_ip))) {
        fatal("inet_ntop failed for destination");
    }
    freeaddrinfo(res);

    struct ip iphdr;
    memset(&iphdr, 0, sizeof(iphdr));
    iphdr.ip_hl = IP4_HDRLEN / sizeof(uint32_t);
    iphdr.ip_v = 4;
    iphdr.ip_tos = 0;
    iphdr.ip_len = htons(IP4_HDRLEN + TCP_HDRLEN);
    iphdr.ip_id = htons(0);
    iphdr.ip_off = htons(0);
    iphdr.ip_ttl = (uint8_t)settings->ttl;
    iphdr.ip_p = IPPROTO_TCP;
    if (inet_pton(AF_INET, src_ip, &iphdr.ip_src) != 1) {
        fatal("inet_pton failed for source address");
    }
    if (inet_pton(AF_INET, dst_ip, &iphdr.ip_dst) != 1) {
        fatal("inet_pton failed for destination address");
    }
    iphdr.ip_sum = 0;
    iphdr.ip_sum = ip_checksum((uint16_t *)&iphdr, IP4_HDRLEN);

    struct tcphdr tcphdr;
    memset(&tcphdr, 0, sizeof(tcphdr));
    tcphdr.th_sport = htons((uint16_t)(settings->source_port > 0 ? settings->source_port : 54321));
    tcphdr.th_dport = htons((uint16_t)settings->dest_port_tcp_head);
    tcphdr.th_seq = htonl(0);
    tcphdr.th_ack = htonl(0);
    tcphdr.th_off = TCP_HDRLEN / 4;
    tcphdr.th_flags = TH_SYN;
    tcphdr.th_win = htons(65535);
    tcphdr.th_urp = htons(0);
    tcphdr.th_sum = tcp4_checksum(iphdr, tcphdr);

    uint8_t *packet = xmalloc(IP_MAXPACKET);
    memcpy(packet, &iphdr, IP4_HDRLEN);
    memcpy(packet + IP4_HDRLEN, &tcphdr, TCP_HDRLEN);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = iphdr.ip_dst.s_addr;

    int sd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sd < 0) {
        fatal("socket(AF_INET, SOCK_RAW) failed — try running as root");
    }
    int on = 1;
    if (setsockopt(sd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0) {
        fatal("setsockopt IP_HDRINCL failed");
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interface);
#ifdef SIOCGIFINDEX
    ifr.ifr_ifindex = ifindex;
#endif
#ifdef SO_BINDTODEVICE
    if (setsockopt(sd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
        perror("warning: SO_BINDTODEVICE");
    }
#else
    (void)ifr;
    (void)ifindex;
#endif

    char errbuf[PCAP_ERRBUF_SIZE];
    char filter_exp[128];
    snprintf(filter_exp, sizeof(filter_exp), "src host %s and tcp", settings->server_ip);

    g_pcap = pcap_open_live(interface, BUFSIZ, 1, 1000, errbuf);
    if (!g_pcap) {
        fatalf("pcap_open_live(%s) failed: %s", interface, errbuf);
    }

    bpf_u_int32 net = 0, mask = 0;
    pcap_lookupnet(interface, &net, &mask, errbuf);
    struct bpf_program filter;
    if (pcap_compile(g_pcap, &filter, filter_exp, 0, net) == -1) {
        fatalf("Bad pcap filter: %s", pcap_geterr(g_pcap));
    }
    if (pcap_setfilter(g_pcap, &filter) == -1) {
        fatalf("Error setting pcap filter: %s", pcap_geterr(g_pcap));
    }

    printf("\n********** Probing Phase **********\n");

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        fatal("Error creating UDP socket");
    }
    udp_set_dont_fragment(udp_sock);

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons((uint16_t)settings->dest_port_udp);
    udp_addr.sin_addr.s_addr = inet_addr(settings->server_ip);

    unsigned char *buffer = xmalloc((size_t)settings->udp_payload_size);
    g_rst_count = 0;

    rebuild_syn_packet(packet, &iphdr, &tcphdr, (uint16_t)settings->dest_port_tcp_head);
    if (send_syn(sd, packet, &sin) < 0) {
        fatal("sendto SYN (low head) failed");
    }
    if (opts.verbose) {
        printf("SYN sent (head / low-entropy)\n");
    }

    alarm(60);
    signal(SIGALRM, alarm_handler);

    fill_low_entropy(buffer, (size_t)settings->udp_payload_size);
    if (send_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                              buffer, udp_sock, udp_addr,
                              settings->inter_packet_us, opts.verbose || 1) < 0) {
        fatal("Failed to send low-entropy packet train");
    }

    rebuild_syn_packet(packet, &iphdr, &tcphdr, (uint16_t)settings->dest_port_tcp_tail);
    if (send_syn(sd, packet, &sin) < 0) {
        fatal("sendto SYN (low tail) failed");
    }

    printf("Waiting intermission (%d s)...\n", settings->inter_time_sec);
    sleep((unsigned int)settings->inter_time_sec);

    rebuild_syn_packet(packet, &iphdr, &tcphdr, (uint16_t)settings->dest_port_tcp_head);
    if (send_syn(sd, packet, &sin) < 0) {
        fatal("sendto SYN (high head) failed");
    }

    fill_high_entropy(buffer, (size_t)settings->udp_payload_size);
    if (send_udp_packet_train(settings->udp_pack_count, settings->udp_payload_size,
                              buffer, udp_sock, udp_addr,
                              settings->inter_packet_us, opts.verbose || 1) < 0) {
        fatal("Failed to send high-entropy packet train");
    }

    rebuild_syn_packet(packet, &iphdr, &tcphdr, (uint16_t)settings->dest_port_tcp_tail);
    if (send_syn(sd, packet, &sin) < 0) {
        fatal("sendto SYN (high tail) failed");
    }

    pcap_loop(g_pcap, 50, packet_handler, NULL);
    pcap_close(g_pcap);
    g_pcap = NULL;

    if (g_rst_count == 4) {
        long low = (long)timeval_delta_ms(g_times[0], g_times[1]);
        long high = (long)timeval_delta_ms(g_times[2], g_times[3]);
        print_detection_result(low, high, settings->threshold_ms, 1, opts.json_mode);
    } else {
        print_detection_result(-1, -1, settings->threshold_ms, 0, opts.json_mode);
        if (!opts.json_mode) {
            printf("Captured %d / 4 RST markers.\n", g_rst_count);
        }
    }

    close(udp_sock);
    close(sd);
    free(buffer);
    free(packet);
    config_free(settings);
    return EXIT_SUCCESS;
}

static uint16_t ip_checksum(uint16_t *addr, int len)
{
    int count = len;
    uint32_t sum = 0;
    while (count > 1) {
        sum += *addr++;
        count -= 2;
    }
    if (count > 0) {
        sum += *(uint8_t *)addr;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t tcp4_checksum(struct ip iphdr, struct tcphdr tcphdr)
{
    char buf[IP_MAXPACKET];
    char *ptr = buf;
    int chksumlen = 0;
    uint16_t svalue;
    char cvalue;

    memcpy(ptr, &iphdr.ip_src.s_addr, sizeof(iphdr.ip_src.s_addr));
    ptr += sizeof(iphdr.ip_src.s_addr);
    chksumlen += (int)sizeof(iphdr.ip_src.s_addr);

    memcpy(ptr, &iphdr.ip_dst.s_addr, sizeof(iphdr.ip_dst.s_addr));
    ptr += sizeof(iphdr.ip_dst.s_addr);
    chksumlen += (int)sizeof(iphdr.ip_dst.s_addr);

    *ptr++ = 0;
    chksumlen += 1;

    memcpy(ptr, &iphdr.ip_p, sizeof(iphdr.ip_p));
    ptr += sizeof(iphdr.ip_p);
    chksumlen += (int)sizeof(iphdr.ip_p);

    svalue = htons(sizeof(tcphdr));
    memcpy(ptr, &svalue, sizeof(svalue));
    ptr += sizeof(svalue);
    chksumlen += (int)sizeof(svalue);

    memcpy(ptr, &tcphdr.th_sport, sizeof(tcphdr.th_sport));
    ptr += sizeof(tcphdr.th_sport);
    chksumlen += (int)sizeof(tcphdr.th_sport);

    memcpy(ptr, &tcphdr.th_dport, sizeof(tcphdr.th_dport));
    ptr += sizeof(tcphdr.th_dport);
    chksumlen += (int)sizeof(tcphdr.th_dport);

    memcpy(ptr, &tcphdr.th_seq, sizeof(tcphdr.th_seq));
    ptr += sizeof(tcphdr.th_seq);
    chksumlen += (int)sizeof(tcphdr.th_seq);

    memcpy(ptr, &tcphdr.th_ack, sizeof(tcphdr.th_ack));
    ptr += sizeof(tcphdr.th_ack);
    chksumlen += (int)sizeof(tcphdr.th_ack);

    cvalue = (char)((tcphdr.th_off << 4) + tcphdr.th_x2);
    memcpy(ptr, &cvalue, sizeof(cvalue));
    ptr += sizeof(cvalue);
    chksumlen += (int)sizeof(cvalue);

    memcpy(ptr, &tcphdr.th_flags, sizeof(tcphdr.th_flags));
    ptr += sizeof(tcphdr.th_flags);
    chksumlen += (int)sizeof(tcphdr.th_flags);

    memcpy(ptr, &tcphdr.th_win, sizeof(tcphdr.th_win));
    ptr += sizeof(tcphdr.th_win);
    chksumlen += (int)sizeof(tcphdr.th_win);

    *ptr++ = 0;
    *ptr++ = 0;
    chksumlen += 2;

    memcpy(ptr, &tcphdr.th_urp, sizeof(tcphdr.th_urp));
    chksumlen += (int)sizeof(tcphdr.th_urp);

    return ip_checksum((uint16_t *)buf, chksumlen);
}
