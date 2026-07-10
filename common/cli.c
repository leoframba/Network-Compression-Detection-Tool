#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_print_usage(const char *prog, int is_server, int is_standalone)
{
    fprintf(stderr, "Usage: %s --config <path> [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --config <path>       JSON configuration file (required)\n");
    fprintf(stderr, "  --threshold <ms>      Detection threshold (default from config / 100)\n");
    fprintf(stderr, "  --trials <n>          Number of measurement trials\n");
    if (is_standalone) {
        fprintf(stderr, "  --interface <name>    Network interface (e.g. eth0, en0)\n");
    }
    if (is_server) {
        fprintf(stderr, "  --listen-port <port>  TCP listen port (overrides config)\n");
    }
    fprintf(stderr, "  --json                Emit machine-readable JSON results\n");
    fprintf(stderr, "  --verbose             Extra logging\n");
    fprintf(stderr, "  -h, --help            Show this help\n");
}

int cli_parse(int argc, char **argv, cli_options *opts, int allow_listen_port)
{
    memset(opts, 0, sizeof(*opts));
    opts->threshold_ms = -1;
    opts->trials = 0;
    opts->listen_port = 0;

    /* Backward-compatible: bare config path as argv[1] */
    if (argc == 2 && argv[1][0] != '-') {
        opts->config_path = argv[1];
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            opts->show_help = 1;
            return 0;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            opts->config_path = argv[++i];
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            opts->threshold_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--trials") == 0 && i + 1 < argc) {
            opts->trials = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            opts->interface = argv[++i];
        } else if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
            if (!allow_listen_port) {
                fprintf(stderr, "Unknown option: --listen-port\n");
                return -1;
            }
            opts->listen_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--json") == 0) {
            opts->json_mode = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (!opts->show_help && !opts->config_path) {
        fprintf(stderr, "Missing required --config <path>\n");
        return -1;
    }
    return 0;
}
