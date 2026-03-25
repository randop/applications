#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <stddef.h>

/* ── Credential record ─────────────────────────────────────────────────── */
typedef struct {
    char *protocol;   /* "https", "ssh", … */
    char *host;       /* "github.com"       */
    char *username;
    char *password;
    char *path;       /* optional URL path  */
} Credential;

/* ── Store file path (default: ~/.git-credentials-store) ─────────────── */
void  store_set_path(const char *path);
const char *store_get_path(void);

/* ── CRUD ──────────────────────────────────────────────────────────────── */
int   store_save  (const Credential *cred);   /* insert or update */
int   store_find  (const char *protocol, const char *host,
                   const char *username, Credential *out);
int   store_remove(const char *protocol, const char *host,
                   const char *username);
int   store_list  (Credential **out, size_t *count);   /* caller frees each */
void  store_free_list(Credential *list, size_t count);

/* ── Helpers ───────────────────────────────────────────────────────────── */
void  credential_free(Credential *c);
void  credential_print(const Credential *c, int show_password);

/* ── Git credential protocol (stdin key=value) ─────────────────────────── */
int   credential_read_from_stdin(Credential *c);   /* fills fields from git */
void  credential_write_to_stdout(const Credential *c); /* git fill response  */

#endif /* CREDENTIAL_STORE_H */
