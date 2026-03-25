/*******************************************************************************
 * argtable3 — ANSI C command-line parsing library
 * Minimal self-contained implementation for git-credential-helper
 * Based on argtable3 by Stewart Heitmann / Tom G. Huang (BSD-2-Clause)
 ******************************************************************************/
#include "argtable3.h"
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

/* ─────────────────────────── internal helpers ─────────────────────────── */

#define ARG_OPTARGMAX  1024   /* max length of an option argument */

/* Split "a,b,c" short-opt string into individual chars */
static int shortopts_contain(const char *shortopts, int c)
{
    if (!shortopts) return 0;
    while (*shortopts) {
        if (*shortopts++ == c) return 1;
    }
    return 0;
}

/* Test if argv[i] matches a long option like --foo or --foo=val */
static int longopt_match(const char *longopts, const char *arg,
                          const char **valp)
{
    if (!longopts || strncmp(arg, "--", 2) != 0) return 0;
    const char *p = arg + 2;
    /* iterate comma-separated names */
    while (*longopts) {
        const char *q = longopts;
        size_t n = 0;
        while (*q && *q != ',') { q++; n++; }
        if (strncmp(p, longopts, n) == 0) {
            if (p[n] == 0) { *valp = NULL;    return 1; }
            if (p[n] == '=') { *valp = p+n+1; return 1; }
        }
        longopts = q;
        if (*longopts == ',') longopts++;
    }
    return 0;
}

/* ─────────────────────────── arg_end ──────────────────────────────────── */

#define ARG_END_MAXERRORS 64

static void end_error(void *parent, FILE *fp, int errcode,
                      const char *argval, const char *progname)
{
    (void)fp; (void)progname;
    arg_end *end = (arg_end *)parent;
    if (end->count < end->hdr.maxcount) {
        end->error [end->count] = errcode;
        end->argval[end->count] = argval;
        end->count++;
    }
}

static void end_reset(void *parent)
{
    arg_end *end = (arg_end *)parent;
    end->count = 0;
}

arg_end *arg_end_new(int maxcount)
{
    /* We store error codes + argval pointers right after the struct */
    size_t sz = sizeof(arg_end)
              + (size_t)maxcount * sizeof(int)
              + (size_t)maxcount * sizeof(char *);
    arg_end *end = (arg_end *)calloc(1, sz);
    if (!end) return NULL;
    end->hdr.flag      = 0;
    end->hdr.shortopts = NULL;
    end->hdr.longopts  = NULL;
    end->hdr.datatype  = NULL;
    end->hdr.glossary  = NULL;
    end->hdr.mincount  = 1;
    end->hdr.maxcount  = maxcount;
    end->hdr.parent    = end;
    end->hdr.resetfn   = end_reset;
    end->hdr.scanfn    = NULL;
    end->hdr.checkfn   = NULL;
    end->hdr.errorfn   = end_error;
    end->hdr.glossaryfn= NULL;
    end->count         = 0;
    end->error  = (int *)         ((char*)end + sizeof(arg_end));
    end->argval = (const char **) ((char*)end + sizeof(arg_end) + (size_t)maxcount*sizeof(int));
    return end;
}

/* ─────────────────────────── arg_lit ──────────────────────────────────── */

static void lit_reset (void *p) { ((arg_lit*)p)->count = 0; }

static int lit_scan(void *parent, const char *argval)
{
    (void)argval;
    ((arg_lit*)parent)->count++;
    return 0;
}

static int lit_check(void *parent)
{
    arg_lit *a = (arg_lit *)parent;
    if (a->count < a->hdr.mincount) return 1;
    if (a->count > a->hdr.maxcount) return 2;
    return 0;
}

static arg_lit *arg_lit_alloc(const char *shortopts, const char *longopts,
                               int mincount, int maxcount, const char *glossary)
{
    arg_lit *a = (arg_lit *)calloc(1, sizeof(arg_lit));
    if (!a) return NULL;
    a->hdr.shortopts = shortopts;
    a->hdr.longopts  = longopts;
    a->hdr.datatype  = NULL;
    a->hdr.glossary  = glossary;
    a->hdr.mincount  = mincount;
    a->hdr.maxcount  = maxcount;
    a->hdr.parent    = a;
    a->hdr.resetfn   = lit_reset;
    a->hdr.scanfn    = lit_scan;
    a->hdr.checkfn   = lit_check;
    a->hdr.errorfn   = NULL;
    a->hdr.glossaryfn= NULL;
    a->count = 0;
    return a;
}

arg_lit *arg_lit0(const char *s,const char *l,const char *g){ return arg_lit_alloc(s,l,0,1,g); }
arg_lit *arg_lit1(const char *s,const char *l,const char *g){ return arg_lit_alloc(s,l,1,1,g); }
arg_lit *arg_litn(const char *s,const char *l,int mn,int mx,const char *g){ return arg_lit_alloc(s,l,mn,mx,g); }

/* ─────────────────────────── arg_str ──────────────────────────────────── */

static void str_reset(void *p) { ((arg_str*)p)->count = 0; }

static int str_scan(void *parent, const char *argval)
{
    arg_str *a = (arg_str *)parent;
    if (a->count < a->hdr.maxcount)
        a->sval[a->count++] = argval;
    return 0;
}

static int str_check(void *parent)
{
    arg_str *a = (arg_str *)parent;
    if (a->count < a->hdr.mincount) return 1;
    if (a->count > a->hdr.maxcount) return 2;
    return 0;
}

static arg_str *arg_str_alloc(const char *shortopts, const char *longopts,
                               const char *datatype, int mincount, int maxcount,
                               const char *glossary)
{
    size_t sz = sizeof(arg_str) + (size_t)maxcount * sizeof(char *);
    arg_str *a = (arg_str *)calloc(1, sz);
    if (!a) return NULL;
    a->hdr.shortopts = shortopts;
    a->hdr.longopts  = longopts;
    a->hdr.datatype  = datatype ? datatype : "<str>";
    a->hdr.glossary  = glossary;
    a->hdr.mincount  = mincount;
    a->hdr.maxcount  = maxcount;
    a->hdr.parent    = a;
    a->hdr.resetfn   = str_reset;
    a->hdr.scanfn    = str_scan;
    a->hdr.checkfn   = str_check;
    a->hdr.errorfn   = NULL;
    a->hdr.glossaryfn= NULL;
    a->count = 0;
    a->sval  = (const char **)((char*)a + sizeof(arg_str));
    return a;
}

arg_str *arg_str0(const char *s,const char *l,const char *dt,const char *g){ return arg_str_alloc(s,l,dt,0,1,g); }
arg_str *arg_str1(const char *s,const char *l,const char *dt,const char *g){ return arg_str_alloc(s,l,dt,1,1,g); }
arg_str *arg_strn(const char *s,const char *l,const char *dt,int mn,int mx,const char *g){ return arg_str_alloc(s,l,dt,mn,mx,g); }

/* ─────────────────────────── arg_int ──────────────────────────────────── */

static void int_reset(void *p) { ((arg_int*)p)->count = 0; }

static int int_scan(void *parent, const char *argval)
{
    arg_int *a = (arg_int *)parent;
    if (!argval) return 1;
    char *end;
    long v = strtol(argval, &end, 0);
    if (*end != '\0') return 1;
    if (a->count < a->hdr.maxcount)
        a->ival[a->count++] = (int)v;
    return 0;
}

static int int_check(void *parent)
{
    arg_int *a = (arg_int *)parent;
    if (a->count < a->hdr.mincount) return 1;
    if (a->count > a->hdr.maxcount) return 2;
    return 0;
}

static arg_int *arg_int_alloc(const char *s,const char *l,const char *dt,
                               int mn,int mx,const char *g)
{
    size_t sz = sizeof(arg_int) + (size_t)mx * sizeof(int);
    arg_int *a = (arg_int *)calloc(1, sz);
    if (!a) return NULL;
    a->hdr.shortopts = s; a->hdr.longopts = l;
    a->hdr.datatype  = dt ? dt : "<int>";
    a->hdr.glossary  = g;
    a->hdr.mincount  = mn; a->hdr.maxcount = mx;
    a->hdr.parent    = a;
    a->hdr.resetfn   = int_reset;
    a->hdr.scanfn    = int_scan;
    a->hdr.checkfn   = int_check;
    a->ival = (int *)((char*)a + sizeof(arg_int));
    return a;
}

arg_int *arg_int0(const char *s,const char *l,const char *dt,const char *g){ return arg_int_alloc(s,l,dt,0,1,g); }
arg_int *arg_int1(const char *s,const char *l,const char *dt,const char *g){ return arg_int_alloc(s,l,dt,1,1,g); }
arg_int *arg_intn(const char *s,const char *l,const char *dt,int mn,int mx,const char *g){ return arg_int_alloc(s,l,dt,mn,mx,g); }

/* ─────────────────────────── arg_rem / arg_file ───────────────────────── */

arg_rem *arg_rem0(const char *dt, const char *g)
{
    arg_rem *a = (arg_rem *)calloc(1, sizeof(arg_rem));
    if (!a) return NULL;
    a->hdr.datatype  = dt;
    a->hdr.glossary  = g;
    a->hdr.mincount  = 1; a->hdr.maxcount = 1;
    return a;
}

/* arg_file — thin wrapper around arg_str */
static arg_file *arg_file_alloc(const char *s,const char *l,const char *dt,
                                 int mn,int mx,const char *g)
{
    size_t sz = sizeof(arg_file)
              + (size_t)mx * sizeof(char *) /* filename */
              + (size_t)mx * sizeof(char *) /* basename */
              + (size_t)mx * sizeof(char *);/* extension */
    arg_file *a = (arg_file *)calloc(1, sz);
    if (!a) return NULL;
    a->hdr.shortopts = s; a->hdr.longopts = l;
    a->hdr.datatype  = dt ? dt : "<file>";
    a->hdr.glossary  = g;
    a->hdr.mincount  = mn; a->hdr.maxcount = mx;
    a->hdr.parent    = a;
    /* reuse str_scan / str_check by pointing to filename array */
    a->hdr.resetfn   = str_reset;
    /* cast through void* to silence -Wcast-function-type; behaviour is defined
       because the actual call sites in arg_parse always pass the right type */
    a->hdr.scanfn    = (arg_scanfn)(void *)str_scan;
    a->hdr.checkfn   = (arg_checkfn)(void *)str_check;
    a->filename  = (const char **)((char*)a + sizeof(arg_file));
    a->basename  = a->filename + mx;
    a->extension = a->basename + mx;
    /* count is at same offset as arg_str.count — works because it's first field */
    return a;
}

arg_file *arg_file0(const char *s,const char *l,const char *dt,const char *g){ return arg_file_alloc(s,l,dt,0,1,g); }
arg_file *arg_file1(const char *s,const char *l,const char *dt,const char *g){ return arg_file_alloc(s,l,dt,1,1,g); }

/* ─────────────────────────── arg_nullcheck ────────────────────────────── */

int arg_nullcheck(void **argtable)
{
    if (!argtable) return 1;
    for (int i = 0; argtable[i]; i++)
        if (!argtable[i]) return 1;
    return 0;
}

/* ─────────────────────────── arg_parse ────────────────────────────────── */

/* Check if this entry is the arg_end sentinel */
static int is_end_entry(void *entry)
{
    arg_hdr_t *h = (arg_hdr_t *)entry;
    return (h->scanfn == NULL && h->errorfn == end_error);
}

/* Find arg_end in table */
static arg_end *find_end(void **argtable)
{
    for (int i = 0; argtable[i]; i++) {
        if (is_end_entry(argtable[i]))
            return (arg_end *)argtable[i];
    }
    return NULL;
}

static void report_error(arg_end *end, int code, const char *argval)
{
    if (end && end->count < end->hdr.maxcount) {
        end->error [end->count] = code;
        end->argval[end->count] = argval;
        end->count++;
    }
}

/* Reset all entries */

static void reset_all(void **argtable)
{
    for (int i = 0; argtable[i]; i++) {
        arg_hdr_t *h = (arg_hdr_t *)argtable[i];
        if (h->resetfn) h->resetfn(argtable[i]);
        if (is_end_entry(argtable[i])) break;
    }
}

/* Find matching entry for a short option char */
static arg_hdr_t *match_short(void **argtable, int c)
{
    for (int i = 0; argtable[i]; i++) {
        if (is_end_entry(argtable[i])) break;
        arg_hdr_t *h = (arg_hdr_t *)argtable[i];
        if (shortopts_contain(h->shortopts, c)) return h;
    }
    return NULL;
}

/* Find matching entry for a long option (sets *valp) */
static arg_hdr_t *match_long(void **argtable, const char *arg, const char **valp)
{
    for (int i = 0; argtable[i]; i++) {
        if (is_end_entry(argtable[i])) break;
        arg_hdr_t *h = (arg_hdr_t *)argtable[i];
        if (longopt_match(h->longopts, arg, valp)) return h;
    }
    return NULL;
}

int arg_parse(int argc, char **argv, void **argtable)
{
    arg_end *end = find_end(argtable);
    reset_all(argtable);
    if (!end) return 1;

    int i = 1; /* skip program name */
    while (i < argc) {
        const char *arg = argv[i];

        /* ── long option ── */
        if (strncmp(arg, "--", 2) == 0 && arg[2] != '\0') {
            const char *val = NULL;
            arg_hdr_t  *h   = match_long(argtable, arg, &val);
            if (!h) {
                report_error(end, 1, arg);
                i++; continue;
            }
            /* Literal flags need no value */
            if (h->datatype == NULL) {
                if (h->scanfn) h->scanfn(h->parent, NULL);
            } else {
                /* value either in --opt=val or next argv */
                if (!val) {
                    if (i+1 < argc) val = argv[++i];
                    else { report_error(end, 2, arg); i++; continue; }
                }
                if (h->scanfn && h->scanfn(h->parent, val) != 0)
                    report_error(end, 3, val);
            }
            i++; continue;
        }

        /* ── short option ── */
        if (arg[0] == '-' && arg[1] != '-' && arg[1] != '\0') {
            /* iterate bundled short opts: -abc */
            int j = 1;
            while (arg[j]) {
                int c = (unsigned char)arg[j];
                arg_hdr_t *h = match_short(argtable, c);
                if (!h) {
                    report_error(end, 1, NULL);
                    j++; continue;
                }
                if (h->datatype == NULL) {
                    if (h->scanfn) h->scanfn(h->parent, NULL);
                    j++;
                } else {
                    /* value is rest of token or next argv */
                    const char *val = &arg[j+1];
                    if (*val == '\0') {
                        if (i+1 < argc) val = argv[++i];
                        else { report_error(end, 2, argv[i]); break; }
                    }
                    if (h->scanfn && h->scanfn(h->parent, val) != 0)
                        report_error(end, 3, val);
                    break; /* consumed rest of token */
                }
            }
            i++; continue;
        }

        /* ── positional / bare argument: feed to first unfilled str/file ── */
        int consumed = 0;
        for (int k = 0; argtable[k]; k++) {
            if (is_end_entry(argtable[k])) break;
            arg_hdr_t *h = (arg_hdr_t *)argtable[k];
            if (h->shortopts == NULL && h->longopts == NULL &&
                h->datatype  != NULL && h->scanfn   != NULL)
            {
                /* check not already full */
                arg_str *as = (arg_str *)argtable[k];
                if (as->count < h->maxcount) {
                    if (h->scanfn(argtable[k], arg) != 0)
                        report_error(end, 3, arg);
                    consumed = 1;
                    break;
                }
            }
        }
        if (!consumed) report_error(end, 4, arg);
        i++;
    }

    /* ── check min/max counts ── */
    for (int k = 0; argtable[k]; k++) {
        arg_hdr_t *h = (arg_hdr_t *)argtable[k];
        if (is_end_entry(argtable[k])) break;
        if (h->checkfn) {
            int r = h->checkfn(argtable[k]);
            if (r == 1) report_error(end, 5, h->longopts ? h->longopts : h->shortopts);
        }
    }
    return end->count;
}

/* ─────────────────────────── print helpers ────────────────────────────── */

static void print_option(FILE *fp, arg_hdr_t *h, int optional)
{
    if (optional) fprintf(fp, "[");
    if (h->shortopts) {
        fprintf(fp, "-%c", h->shortopts[0]);
        if (h->datatype) fprintf(fp, " %s", h->datatype);
    } else if (h->longopts) {
        /* print first long opt */
        const char *p = h->longopts;
        fprintf(fp, "--");
        while (*p && *p != ',') fputc(*p++, fp);
        if (h->datatype) fprintf(fp, "=%s", h->datatype);
    } else if (h->datatype) {
        fprintf(fp, "%s", h->datatype);
    }
    if (optional) fprintf(fp, "]");
}

void arg_print_syntax(FILE *fp, void **argtable, const char *suffix)
{
    for (int i = 0; argtable[i]; i++) {
        arg_hdr_t *h = (arg_hdr_t *)argtable[i];
        if (!h->scanfn && h->errorfn == end_error) break; /* arg_end */
        if (!h->glossary && !h->datatype) continue;       /* arg_rem */
        int opt = (h->mincount == 0);
        fprintf(fp, " ");
        print_option(fp, h, opt);
    }
    if (suffix) fprintf(fp, "%s", suffix);
}

void arg_print_syntaxv(FILE *fp, void **argtable, const char *suffix)
{
    arg_print_syntax(fp, argtable, suffix);
}

void arg_print_glossary(FILE *fp, void **argtable, const char *format)
{
    for (int i = 0; argtable[i]; i++) {
        arg_hdr_t *h = (arg_hdr_t *)argtable[i];
        if (is_end_entry(argtable[i])) break;
        if (!h->glossary) continue;
        char left[80] = {0};
        if (h->shortopts && h->longopts) {
            /* pick first long */
            char lname[64]; int j=0;
            const char *p = h->longopts;
            while (*p && *p != ',' && j<63) lname[j++]=*p++;
            lname[j]=0;
            if (h->datatype)
                snprintf(left,sizeof(left),"  -%c, --%s=%s", h->shortopts[0], lname, h->datatype);
            else
                snprintf(left,sizeof(left),"  -%c, --%s", h->shortopts[0], lname);
        } else if (h->longopts) {
            char lname[64]; int j=0;
            const char *p = h->longopts;
            while (*p && *p != ',' && j<63) lname[j++]=*p++;
            lname[j]=0;
            if (h->datatype)
                snprintf(left,sizeof(left),"      --%s=%s", lname, h->datatype);
            else
                snprintf(left,sizeof(left),"      --%s", lname);
        } else if (h->shortopts) {
            if (h->datatype)
                snprintf(left,sizeof(left),"  -%c %s", h->shortopts[0], h->datatype);
            else
                snprintf(left,sizeof(left),"  -%c", h->shortopts[0]);
        } else if (h->datatype) {
            snprintf(left,sizeof(left),"  %s", h->datatype);
        }
        /* use format or default */
        const char *fmt = format ? format : "  %-25s %s\n";
        fprintf(fp, fmt, left, h->glossary);
    }
}

void arg_print_glossary_gnu(FILE *fp, void **argtable)
{
    arg_print_glossary(fp, argtable, NULL);
}

void arg_print_errors(FILE *fp, arg_end *end, const char *progname)
{
    static const char *msgs[] = {
        "unknown error",
        "unrecognized option",
        "option requires an argument",
        "invalid argument",
        "unexpected argument",
        "required option missing",
    };
    for (int i = 0; i < end->count; i++) {
        int code = end->error[i];
        const char *val = end->argval[i];
        const char *msg = (code >= 0 && code <= 5) ? msgs[code] : msgs[0];
        if (val)
            fprintf(fp, "%s: %s '%s'\n", progname, msg, val);
        else
            fprintf(fp, "%s: %s\n", progname, msg);
    }
}

void arg_freetable(void **argtable, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (argtable[i]) { free(argtable[i]); argtable[i] = NULL; }
    }
}
