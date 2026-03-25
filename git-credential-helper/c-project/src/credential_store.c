/*
 * credential_store.c — file-backed credential storage for git-credential-helper
 *
 * Store format (one entry per line, tab-separated):
 *   PROTOCOL\tHOST\tUSERNAME\tPASSWORD_B64\tPATH
 *
 * Passwords are base64-encoded to survive special characters.
 * The file is created with mode 0600 (owner read/write only).
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "credential_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

/* ─────────────────────────── paths ────────────────────────────────────── */

static char s_path[4096] = {0};

void store_set_path(const char *path)
{
    strncpy(s_path, path, sizeof(s_path)-1);
}

const char *store_get_path(void)
{
    if (s_path[0]) return s_path;
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(s_path, sizeof(s_path), "%s/.git-credentials-store", home);
    return s_path;
}

/* ─────────────────────────── base64 ────────────────────────────────────── */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode(const char *src)
{
    size_t n = strlen(src);
    size_t out_len = 4 * ((n + 2) / 3) + 1;
    char *out = malloc(out_len);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned int v = (unsigned char)src[i] << 16;
        if (i+1 < n) v |= (unsigned char)src[i+1] << 8;
        if (i+2 < n) v |= (unsigned char)src[i+2];
        out[j++] = B64[(v>>18)&0x3F];
        out[j++] = B64[(v>>12)&0x3F];
        out[j++] = (i+1 < n) ? B64[(v>>6)&0x3F]  : '=';
        out[j++] = (i+2 < n) ? B64[v      &0x3F]  : '=';
    }
    out[j] = '\0';
    return out;
}

static int b64_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static char *b64_decode(const char *src)
{
    size_t n = strlen(src);
    size_t out_max = (n * 3) / 4 + 4;
    char *out = malloc(out_max);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i+3 < n; i += 4) {
        int a = b64_char(src[i]),   b = b64_char(src[i+1]);
        int c = (src[i+2]=='=') ? 0 : b64_char(src[i+2]);
        int d = (src[i+3]=='=') ? 0 : b64_char(src[i+3]);
        if (a<0||b<0) break;
        out[j++] = (char)((a<<2)|(b>>4));
        if (src[i+2] != '=') out[j++] = (char)((b<<4)|(c>>2));
        if (src[i+3] != '=') out[j++] = (char)((c<<6)|d);
    }
    out[j] = '\0';
    return out;
}

/* ─────────────────────────── helpers ──────────────────────────────────── */

static char *dupstr(const char *s)
{
    if (!s) return NULL;
    return strdup(s);
}

void credential_free(Credential *c)
{
    if (!c) return;
    free(c->protocol); free(c->host);
    free(c->username); free(c->password); free(c->path);
    memset(c, 0, sizeof(*c));
}

void credential_print(const Credential *c, int show_password)
{
    printf("  protocol : %s\n", c->protocol  ? c->protocol  : "(none)");
    printf("  host     : %s\n", c->host      ? c->host      : "(none)");
    printf("  username : %s\n", c->username  ? c->username  : "(none)");
    if (show_password)
        printf("  password : %s\n", c->password ? c->password : "(none)");
    else
        printf("  password : %s\n", c->password ? "********"   : "(none)");
    if (c->path && c->path[0])
        printf("  path     : %s\n", c->path);
}

/* ─────────────────────────── parse a store line ────────────────────────── */

/* Splits a tab-delimited line into a Credential (returns 0 on success) */
static int parse_line(char *line, Credential *c)
{
    memset(c, 0, sizeof(*c));
    /* strip newline */
    char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
    char *cr = strchr(line, '\r'); if (cr) *cr = '\0';
    if (line[0] == '#' || line[0] == '\0') return -1;

    char *tok, *rest = line;
    #define NEXT_FIELD(dst) \
        tok = strsep(&rest, "\t"); if (!tok) return -1; (dst) = tok[0] ? dupstr(tok) : NULL;

    NEXT_FIELD(c->protocol);
    NEXT_FIELD(c->host);
    NEXT_FIELD(c->username);
    /* password is base64-encoded */
    tok = strsep(&rest, "\t");
    if (!tok) return -1;
    c->password = tok[0] ? b64_decode(tok) : NULL;
    /* optional path */
    if (rest && rest[0]) c->path = dupstr(rest);
    return 0;
    #undef NEXT_FIELD
}

/* ─────────────────────────── read entire store ─────────────────────────── */

typedef struct { Credential *items; size_t count; size_t cap; } CredList;

static void cl_append(CredList *cl, const Credential *c)
{
    if (cl->count == cl->cap) {
        cl->cap = cl->cap ? cl->cap * 2 : 8;
        cl->items = realloc(cl->items, cl->cap * sizeof(Credential));
    }
    cl->items[cl->count++] = *c;
}

static CredList load_all(void)
{
    CredList cl = {NULL, 0, 0};
    FILE *fp = fopen(store_get_path(), "r");
    if (!fp) return cl;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        Credential c;
        if (parse_line(buf, &c) == 0)
            cl_append(&cl, &c);
    }
    fclose(fp);
    return cl;
}

static int save_all(const CredList *cl)
{
    const char *path = store_get_path();
    /* write to temp then rename (atomic) */
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
    int fd = open(tmp, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd < 0) { perror("open"); return -1; }
    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return -1; }
    fprintf(fp, "# git-credential-helper store — do not edit manually\n");
    for (size_t i = 0; i < cl->count; i++) {
        const Credential *c = &cl->items[i];
        char *enc = b64_encode(c->password ? c->password : "");
        fprintf(fp, "%s\t%s\t%s\t%s\t%s\n",
                c->protocol  ? c->protocol  : "",
                c->host      ? c->host      : "",
                c->username  ? c->username  : "",
                enc          ? enc          : "",
                c->path      ? c->path      : "");
        free(enc);
    }
    fclose(fp);
    if (rename(tmp, path) != 0) { perror("rename"); unlink(tmp); return -1; }
    return 0;
}

static void cl_free(CredList *cl)
{
    for (size_t i = 0; i < cl->count; i++) credential_free(&cl->items[i]);
    free(cl->items);
    cl->items = NULL; cl->count = cl->cap = 0;
}

/* ─────────────────────────── public CRUD ───────────────────────────────── */

int store_save(const Credential *cred)
{
    CredList cl = load_all();
    /* look for existing entry to update */
    for (size_t i = 0; i < cl.count; i++) {
        Credential *c = &cl.items[i];
        int proto_match = (!cred->protocol || !c->protocol ||
                           strcmp(cred->protocol, c->protocol) == 0);
        int host_match  = (cred->host && c->host &&
                           strcmp(cred->host, c->host) == 0);
        int user_match  = (!cred->username || !c->username ||
                           strcmp(cred->username, c->username) == 0);
        if (proto_match && host_match && user_match) {
            /* update password */
            free(c->password);
            c->password = cred->password ? dupstr(cred->password) : NULL;
            if (cred->username && !c->username)
                c->username = dupstr(cred->username);
            int r = save_all(&cl);
            cl_free(&cl);
            return r;
        }
    }
    /* new entry */
    Credential copy = {
        dupstr(cred->protocol), dupstr(cred->host),
        dupstr(cred->username), dupstr(cred->password), dupstr(cred->path)
    };
    cl_append(&cl, &copy);
    int r = save_all(&cl);
    cl_free(&cl);
    return r;
}

int store_find(const char *protocol, const char *host,
               const char *username, Credential *out)
{
    CredList cl = load_all();
    int found = 0;
    for (size_t i = 0; i < cl.count; i++) {
        Credential *c = &cl.items[i];
        int p = (!protocol || !c->protocol || strcmp(protocol, c->protocol)==0);
        int h = (host && c->host && strcmp(host, c->host)==0);
        int u = (!username || !c->username || strcmp(username, c->username)==0);
        if (p && h && u) {
            out->protocol = dupstr(c->protocol);
            out->host     = dupstr(c->host);
            out->username = dupstr(c->username);
            out->password = dupstr(c->password);
            out->path     = dupstr(c->path);
            found = 1;
            break;
        }
    }
    cl_free(&cl);
    return found ? 0 : -1;
}

int store_remove(const char *protocol, const char *host, const char *username)
{
    CredList cl = load_all(), out = {NULL,0,0};
    int removed = 0;
    for (size_t i = 0; i < cl.count; i++) {
        Credential *c = &cl.items[i];
        int p = (protocol && c->protocol && strcmp(protocol, c->protocol)==0);
        int h = (host     && c->host     && strcmp(host,     c->host)    ==0);
        int u = (!username || !c->username || strcmp(username,c->username)==0);
        if (p && h && u) { credential_free(c); removed++; }
        else             { cl_append(&out, c); /* shallow copy */ }
    }
    int r = (removed > 0) ? save_all(&out) : -1;
    free(cl.items); /* don't deep-free, items moved to out */
    cl_free(&out);
    return r;
}

int store_list(Credential **outp, size_t *count)
{
    CredList cl = load_all();
    *outp  = cl.items;
    *count = cl.count;
    return 0;
}

void store_free_list(Credential *list, size_t count)
{
    for (size_t i = 0; i < count; i++) credential_free(&list[i]);
    free(list);
}

/* ─────────────────────────── git protocol I/O ──────────────────────────── */

/*
 * Git sends  "key=value\n" lines, terminated by a blank line.
 * We parse: protocol, host, username, password, path
 */
int credential_read_from_stdin(Credential *c)
{
    memset(c, 0, sizeof(*c));
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        /* strip newline */
        char *nl = strchr(line, '\n'); if (nl) *nl='\0';
        char *cr = strchr(line, '\r'); if (cr) *cr='\0';
        if (line[0] == '\0') break; /* blank line = end of input */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if      (strcmp(key,"protocol")==0) c->protocol = dupstr(val);
        else if (strcmp(key,"host")    ==0) c->host     = dupstr(val);
        else if (strcmp(key,"username")==0) c->username = dupstr(val);
        else if (strcmp(key,"password")==0) c->password = dupstr(val);
        else if (strcmp(key,"path")    ==0) c->path     = dupstr(val);
    }
    return 0;
}

void credential_write_to_stdout(const Credential *c)
{
    if (c->protocol) printf("protocol=%s\n", c->protocol);
    if (c->host)     printf("host=%s\n",     c->host);
    if (c->username) printf("username=%s\n", c->username);
    if (c->password) printf("password=%s\n", c->password);
    if (c->path)     printf("path=%s\n",     c->path);
    printf("\n");
    fflush(stdout);
}
