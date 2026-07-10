#ifndef NCD_CLI_H
#define NCD_CLI_H

typedef struct cli_options {
    const char *config_path;
    int threshold_ms;      /* -1 = use config */
    int trials;            /* 0 = use config */
    const char *interface; /* NULL = use config */
    int json_mode;
    int verbose;
    int listen_port;       /* 0 = use config (server only) */
    int show_help;
} cli_options;

/** Parse common flags. Returns 0 on success, -1 on error. */
int cli_parse(int argc, char **argv, cli_options *opts, int allow_listen_port);

void cli_print_usage(const char *prog, int is_server, int is_standalone);

#endif /* NCD_CLI_H */
