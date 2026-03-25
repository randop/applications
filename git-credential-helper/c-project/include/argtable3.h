/*******************************************************************************
 * argtable3  — ANSI C command-line parsing library
 * Copyright (C) 1998-2001,2003-2011 Stewart Heitmann
 * Copyright (C) 2012-2023 Tom G. Huang
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Minimal amalgamated header (sufficient for git-credential-helper)
 ******************************************************************************/
#ifndef ARGTABLE3_H
#define ARGTABLE3_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARG_REX_ICASE  1
#define ARG_DSTR_SIZE  200
#define ARG_LONG_MAX   0x7fffffffL

/* ── Shared header fields placed at the start of every arg_xxx struct ── */
typedef void (*arg_resetfn)(void *parent);
typedef int  (*arg_scanfn) (void *parent, const char *argval);
typedef int  (*arg_checkfn)(void *parent);
typedef void (*arg_errorfn)(void *parent, FILE *fp, int error,
                            const char *argval, const char *progname);
typedef void (*arg_glossaryfn)(void *parent, FILE *fp);

typedef struct arg_hdr {
    char         flag;
    const char  *shortopts;
    const char  *longopts;
    const char  *datatype;
    const char  *glossary;
    int          mincount;
    int          maxcount;
    void        *parent;
    arg_resetfn  resetfn;
    arg_scanfn   scanfn;
    arg_checkfn  checkfn;
    arg_errorfn  errorfn;
    arg_glossaryfn glossaryfn;
} arg_hdr_t;

typedef struct arg_rem  { struct arg_hdr hdr; } arg_rem;
typedef struct arg_end  { struct arg_hdr hdr; int count; int *error; const char **argval; } arg_end;

typedef struct arg_lit {
    struct arg_hdr hdr;
    int count;
} arg_lit;

typedef struct arg_int {
    struct arg_hdr hdr;
    int count;
    int *ival;
} arg_int;

typedef struct arg_str {
    struct arg_hdr hdr;
    int count;
    const char **sval;
} arg_str;

typedef struct arg_file {
    struct arg_hdr hdr;
    int count;
    const char **filename;
    const char **basename;
    const char **extension;
} arg_file;

/* ── Constructor prototypes ── */
arg_rem  *arg_rem0  (const char *datatype, const char *glossary);
arg_end  *arg_end_new(int maxcount);
#define   arg_end(n)  arg_end_new(n)   /* convenience macro */
arg_lit  *arg_lit0  (const char *shortopts, const char *longopts, const char *glossary);
arg_lit  *arg_lit1  (const char *shortopts, const char *longopts, const char *glossary);
arg_lit  *arg_litn  (const char *shortopts, const char *longopts, int mincount, int maxcount, const char *glossary);
arg_str  *arg_str0  (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_str  *arg_str1  (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_str  *arg_strn  (const char *shortopts, const char *longopts, const char *datatype, int mincount, int maxcount, const char *glossary);
arg_file *arg_file0 (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_file *arg_file1 (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_int  *arg_int0  (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_int  *arg_int1  (const char *shortopts, const char *longopts, const char *datatype, const char *glossary);
arg_int  *arg_intn  (const char *shortopts, const char *longopts, const char *datatype, int mincount, int maxcount, const char *glossary);

/* ── Core API ── */
int   arg_nullcheck(void **argtable);
int   arg_parse     (int argc, char **argv, void **argtable);
void  arg_print_syntax  (FILE *fp, void **argtable, const char *suffix);
void  arg_print_syntaxv (FILE *fp, void **argtable, const char *suffix);
void  arg_print_glossary(FILE *fp, void **argtable, const char *format);
void  arg_print_glossary_gnu(FILE *fp, void **argtable);
void  arg_print_errors (FILE *fp, arg_end *end, const char *progname);
void  arg_freetable  (void **argtable, size_t n);

#ifdef __cplusplus
}
#endif
#endif /* ARGTABLE3_H */
