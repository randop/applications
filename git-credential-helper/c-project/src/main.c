/*
 * git-credential-helper — a git credential helper with a rich CLI
 *
 * Usage as a git helper (git calls these automatically):
 *   git config credential.helper '/path/to/git-credential-helper'
 *   git-credential-helper get
 *   git-credential-helper store
 *   git-credential-helper erase
 *
 * Direct management commands:
 *   git-credential-helper add    --host github.com --user alice --pass s3cr3t
 *   git-credential-helper list   [--show-passwords]
 *   git-credential-helper remove --host github.com [--user alice]
 *   git-credential-helper config --store-path /path/to/file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "argtable3.h"
#include "credential_store.h"

#define PROG "git-credential-helper"
#define VERSION "1.0.0"

/* ─────────────────────────── password prompt ──────────────────────────── */

static char *prompt_password(const char *prompt)
{
    struct termios old, noecho;
    fprintf(stderr, "%s", prompt);
    fflush(stderr);

    tcgetattr(STDIN_FILENO, &old);
    noecho = old;
    noecho.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ECHONL);
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);

    static char buf[512];
    char *r = fgets(buf, sizeof(buf), stdin);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    fprintf(stderr, "\n");
    if (!r) return NULL;
    char *nl = strchr(buf, '\n'); if (nl) *nl = '\0';
    return buf;
}

/* ─────────────────────────── sub-commands ──────────────────────────────── */

/* git calls: get — read credential from stdin, print to stdout */
static int cmd_get(void)
{
    Credential query, result;
    credential_read_from_stdin(&query);
    if (store_find(query.protocol, query.host, query.username, &result) == 0) {
        credential_write_to_stdout(&result);
        credential_free(&result);
    }
    credential_free(&query);
    return 0;
}

/* git calls: store — read credential from stdin and save it */
static int cmd_store(void)
{
    Credential c;
    credential_read_from_stdin(&c);
    int r = store_save(&c);
    if (r != 0) fprintf(stderr, PROG ": failed to store credential\n");
    credential_free(&c);
    return r;
}

/* git calls: erase — read credential from stdin and delete it */
static int cmd_erase(void)
{
    Credential c;
    credential_read_from_stdin(&c);
    store_remove(c.protocol, c.host, c.username);
    credential_free(&c);
    return 0;
}

/* Direct: add */
static int cmd_add(int argc, char **argv)
{
    struct arg_str  *host     = arg_str1("H","host",    "<host>",     "Hostname (e.g. github.com)");
    struct arg_str  *user     = arg_str1("u","username","<username>", "Username / email");
    struct arg_str  *pass     = arg_str0("p","password","<password>", "Password (prompted if omitted)");
    struct arg_str  *protocol = arg_str0("P","protocol","<proto>",    "Protocol [default: https]");
    struct arg_str  *path     = arg_str0(NULL,"path",  "<path>",      "Optional URL path");
    struct arg_lit  *help     = arg_lit0("h","help",                  "Print this help");
    struct arg_end  *end      = arg_end(20);

    void *argtable[] = { host, user, pass, protocol, path, help, end };
    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count) {
        fprintf(stdout, "Usage: " PROG " add");
        arg_print_syntax(stdout, argtable, "\n");
        fprintf(stdout, "\nAdd or update a stored credential.\n\n");
        arg_print_glossary_gnu(stdout, argtable);
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, PROG " add");
        fprintf(stderr, "Try '" PROG " add --help'\n");
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 1;
    }

    const char *password = (pass->count > 0) ? pass->sval[0] : NULL;
    char *prompted = NULL;
    if (!password) {
        char prompt[256];
        snprintf(prompt, sizeof(prompt), "Password for %s@%s: ",
                 user->sval[0], host->sval[0]);
        prompted = prompt_password(prompt);
        if (!prompted || prompted[0] == '\0') {
            fprintf(stderr, PROG ": password is required\n");
            arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
            return 1;
        }
        password = prompted;
    }

    Credential c = {
        .protocol = (char *)(protocol->count ? protocol->sval[0] : "https"),
        .host     = (char *)host->sval[0],
        .username = (char *)user->sval[0],
        .password = (char *)password,
        .path     = (char *)(path->count ? path->sval[0] : NULL),
    };

    int r = store_save(&c);
    if (r == 0)
        fprintf(stdout, "✓ Credential saved for %s@%s\n", c.username, c.host);
    else
        fprintf(stderr, PROG ": failed to save credential\n");

    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    return r;
}

/* Direct: list */
static int cmd_list(int argc, char **argv)
{
    struct arg_lit  *show_pass = arg_lit0("s","show-passwords", "Reveal stored passwords");
    struct arg_str  *filter    = arg_str0("f","filter","<host>",  "Filter by hostname");
    struct arg_lit  *help      = arg_lit0("h","help",             "Print this help");
    struct arg_end  *end       = arg_end(20);

    void *argtable[] = { show_pass, filter, help, end };
    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count) {
        fprintf(stdout, "Usage: " PROG " list");
        arg_print_syntax(stdout, argtable, "\n");
        fprintf(stdout, "\nList stored credentials.\n\n");
        arg_print_glossary_gnu(stdout, argtable);
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, PROG " list");
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 1;
    }

    Credential *list;
    size_t count;
    store_list(&list, &count);

    const char *flt = filter->count ? filter->sval[0] : NULL;
    size_t shown = 0;
    for (size_t i = 0; i < count; i++) {
        if (flt && list[i].host && strstr(list[i].host, flt) == NULL)
            continue;
        fprintf(stdout, "─── [%zu] ─────────────────────────────\n", shown+1);
        credential_print(&list[i], show_pass->count);
        shown++;
    }
    if (shown == 0)
        fprintf(stdout, "(no credentials stored%s)\n", flt ? " matching filter" : "");
    else
        fprintf(stdout, "─────────────────────────────────────\n"
                        "Total: %zu credential(s)\n", shown);

    store_free_list(list, count);
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    return 0;
}

/* Direct: remove */
static int cmd_remove(int argc, char **argv)
{
    struct arg_str  *host     = arg_str1("H","host",    "<host>",     "Hostname to remove");
    struct arg_str  *user     = arg_str0("u","username","<username>", "Specific username (omit = all users for host)");
    struct arg_str  *protocol = arg_str0("P","protocol","<proto>",    "Protocol filter [default: all]");
    struct arg_lit  *force    = arg_lit0("f","force",                 "Skip confirmation prompt");
    struct arg_lit  *help     = arg_lit0("h","help",                  "Print this help");
    struct arg_end  *end      = arg_end(20);

    void *argtable[] = { host, user, protocol, force, help, end };
    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count) {
        fprintf(stdout, "Usage: " PROG " remove");
        arg_print_syntax(stdout, argtable, "\n");
        fprintf(stdout, "\nRemove stored credential(s).\n\n");
        arg_print_glossary_gnu(stdout, argtable);
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, PROG " remove");
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 1;
    }

    const char *proto = protocol->count ? protocol->sval[0] : NULL;
    const char *uname = user->count     ? user->sval[0]     : NULL;

    if (!force->count) {
        fprintf(stderr, "Remove credential(s) for %s%s%s? [y/N] ",
                host->sval[0],
                uname ? " (user: " : "", uname ? uname : "");
        if (uname) fprintf(stderr, ")");
        fprintf(stderr, " ");
        char ans[8];
        if (!fgets(ans, sizeof(ans), stdin) || (ans[0]!='y' && ans[0]!='Y')) {
            fprintf(stdout, "Aborted.\n");
            arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
            return 0;
        }
    }

    int r = store_remove(proto, host->sval[0], uname);
    if (r == 0)
        fprintf(stdout, "✓ Credential(s) removed.\n");
    else
        fprintf(stderr, PROG ": no matching credential found\n");

    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    return (r == 0) ? 0 : 1;
}

/* Direct: config */
static int cmd_config(int argc, char **argv)
{
    struct arg_str *store_path = arg_str0("s","store-path","<path>","Path to the credentials store file");
    struct arg_lit *show       = arg_lit0(NULL,"show",              "Print current configuration");
    struct arg_lit *help       = arg_lit0("h","help",               "Print this help");
    struct arg_end *end        = arg_end(20);

    void *argtable[] = { store_path, show, help, end };
    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count) {
        fprintf(stdout, "Usage: " PROG " config");
        arg_print_syntax(stdout, argtable, "\n");
        fprintf(stdout, "\nConfigure git-credential-helper.\n\n");
        arg_print_glossary_gnu(stdout, argtable);
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, PROG " config");
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return 1;
    }

    if (store_path->count) {
        store_set_path(store_path->sval[0]);
        fprintf(stdout, "Store path set to: %s\n", store_get_path());
        fprintf(stdout, "(Note: set permanently via GIT_CREDENTIAL_STORE env var "
                        "or pass --store-path on every invocation.)\n");
    }
    if (show->count || store_path->count == 0) {
        fprintf(stdout, "Store path : %s\n", store_get_path());
        fprintf(stdout, "Version    : " VERSION "\n");
    }
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    return 0;
}

/* ─────────────────────────── top-level help ────────────────────────────── */

static void print_top_help(const char *prog)
{
    fprintf(stdout,
        PROG " v" VERSION " — git credential helper\n\n"
        "Usage: %s <command> [options]\n\n"
        "Git helper commands (called automatically by git):\n"
        "  get       Read credentials matching stdin query\n"
        "  store     Save credentials from stdin\n"
        "  erase     Remove credentials matching stdin query\n\n"
        "Management commands:\n"
        "  add       Add or update a credential\n"
        "  list      List stored credentials\n"
        "  remove    Remove a stored credential\n"
        "  config    Show / set configuration\n\n"
        "Run '%s <command> --help' for command-specific options.\n\n"
        "Setup:\n"
        "  git config --global credential.helper '%s'\n\n",
        prog, prog, prog);
}

/* ─────────────────────────── main ─────────────────────────────────────── */

int main(int argc, char **argv)
{
    /* honour GIT_CREDENTIAL_STORE env for store path */
    const char *env_path = getenv("GIT_CREDENTIAL_STORE");
    if (env_path) store_set_path(env_path);

    if (argc < 2) {
        print_top_help(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* shift argv so each sub-command sees its own argc/argv */
    int  sub_argc = argc - 1;
    char **sub_argv = argv + 1;

    if (strcmp(cmd, "get")    == 0) return cmd_get();
    if (strcmp(cmd, "store")  == 0) return cmd_store();
    if (strcmp(cmd, "erase")  == 0) return cmd_erase();
    if (strcmp(cmd, "add")    == 0) return cmd_add   (sub_argc, sub_argv);
    if (strcmp(cmd, "list")   == 0) return cmd_list  (sub_argc, sub_argv);
    if (strcmp(cmd, "remove") == 0) return cmd_remove(sub_argc, sub_argv);
    if (strcmp(cmd, "config") == 0) return cmd_config(sub_argc, sub_argv);

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        fprintf(stdout, PROG " v" VERSION "\n");
        return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_top_help(argv[0]);
        return 0;
    }

    fprintf(stderr, PROG ": unknown command '%s'\n"
                    "Run '" PROG " --help' for usage.\n", cmd);
    return 1;
}
