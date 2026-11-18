/*
 * CC9 - ANSI C (C89) compiler for the Sparrow09 (Hitachi 6309)
 * Generates source code for the BS9 assembler.
 */
#ifndef CC9_H
#define CC9_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>

#define CC9_VERSION "1.0"

typedef struct Type   Type;
typedef struct Token  Token;
typedef struct Node   Node;
typedef struct Obj    Obj;
typedef struct Member Member;
typedef struct Hideset Hideset;

/* ---------------- util.c ---------------- */

void  *xmalloc(size_t n);
void  *xcalloc(size_t n);
char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);
char  *format(const char *fmt, ...);
void   error(const char *fmt, ...);
void   error_tok(Token *tok, const char *fmt, ...);
void   warn_tok(Token *tok, const char *fmt, ...);
extern int warnings_on;

typedef struct { void **data; int len, cap; } Vec;
void vec_push(Vec *v, void *p);
FILE *mem_open(void);
char *mem_close(FILE *fp);

typedef struct HashEntry { char *key; int keylen; void *val; } HashEntry;
typedef struct { HashEntry *buckets; int capacity, used; } HashMap;
void *hm_get(HashMap *m, const char *key);
void *hm_get2(HashMap *m, const char *key, int len);
void  hm_put(HashMap *m, const char *key, void *val);
void  hm_put2(HashMap *m, const char *key, int len, void *val);
void  hm_del(HashMap *m, const char *key);

/* ---------------- tokens ---------------- */

typedef enum {
   TK_IDENT, TK_PUNCT, TK_KEYWORD, TK_STR, TK_NUM, TK_PPNUM, TK_EOF
} TokenKind;

typedef struct File {
   char *name;
   int   file_no;
   char *contents;
   char *display_name;
   int   line_delta;
} File;

struct Token {
   TokenKind kind;
   Token    *next;
   long long val;       /* integer value  */
   double    fval;      /* float value    */
   char     *loc;
   int       len;
   Type     *ty;        /* type of number or string literal */
   char     *str;       /* string literal contents incl. NUL */
   int       strlen;    /* bytes in str incl. NUL */
   File     *file;
   char     *filename;
   int       line_no;
   int       at_bol;
   int       has_space;
   Hideset  *hideset;
   Token    *origin;
};

int    equal(Token *tok, const char *op);
Token *skip(Token *tok, const char *op);
int    consume(Token **rest, Token *tok, const char *str);
Token *tokenize(File *file);
Token *tokenize_file(char *filename);
Token *tokenize_string_literal(Token *tok, Type *basety);
File  *new_file(char *name, int file_no, char *contents);
void   convert_pp_tokens(Token *tok);
char  *tok_str(Token *tok);
File **get_input_files(void);

/* ---------------- preprocessor ---------------- */

extern Vec include_paths;
Token *preprocess(Token *tok);
void   define_macro(char *name, char *buf);
void   undef_macro(char *name);
void   init_macros(void);

/* ---------------- types ---------------- */

typedef enum {
   TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG, TY_FLOAT, TY_DOUBLE,
   TY_LDOUBLE, TY_ENUM, TY_PTR, TY_FUNC, TY_ARRAY, TY_STRUCT, TY_UNION
} TypeKind;

struct Type {
   TypeKind kind;
   int      size;
   int      align;
   int      is_unsigned;
   int      is_const;
   int      is_volatile;
   Type    *origin;       /* for typedef'd qualified copies */
   Type    *base;         /* pointer / array element */
   Token   *name;         /* declarator name */
   int      array_len;    /* -1: incomplete */
   /* struct / union */
   Member  *members;
   int      is_flexible;
   int      is_incomplete;
   /* function */
   Type    *return_ty;
   Type    *params;
   int      is_variadic;
   int      has_proto;    /* prototype present (not old style ()) */
   Type    *next;         /* next parameter */
};

struct Member {
   Member *next;
   Type   *ty;
   Token  *tok;
   Token  *name;
   int     idx;
   int     offset;
   int     is_bitfield;
   int     bit_offset;   /* bit position (LSB = 0) inside 16 bit unit */
   int     bit_width;
};

extern Type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int,
            *ty_uint, *ty_long, *ty_ulong, *ty_float, *ty_double, *ty_ldouble,
            *ty_schar;

int   is_integer(Type *ty);
int   is_flonum(Type *ty);
int   is_numeric(Type *ty);
int   is_scalar(Type *ty);
int   is_compatible(Type *a, Type *b);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
Type *struct_type(void);
void  add_type(Node *node);
extern int is_char_signed;

/* ---------------- AST ---------------- */

struct Obj {
   Obj   *next;
   char  *name;        /* C name */
   char  *asmname;     /* assembler label (globals) */
   Type  *ty;
   Token *tok;
   int    is_local;
   int    offset;      /* locals: offset from U */
   int    is_function;
   int    is_definition;
   int    is_static;
   int    is_extern;
   int    is_tentative;
   int    is_param;
   int    is_used;     /* reachable from main */
   int    is_inline_asm_fn;
   /* global variable init */
   char  *init_data;
   struct Relocation *rel;
   /* function */
   Obj   *params;
   Node  *body;
   Obj   *locals;
   int    stack_size;
   int    hidden_ret;  /* returns double/struct via hidden pointer */
   Vec    refs;        /* referenced globals (for tree shaking) */
   Vec    asms;        /* inline asm strings */
   char  *asmtext;     /* for top level asm() blocks */
   int    file_no;
};

typedef struct Relocation {
   struct Relocation *next;
   int    offset;
   char **label;
   long   addend;
} Relocation;

typedef enum {
   ND_NULL_EXPR, ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_NEG, ND_MOD,
   ND_BITAND, ND_BITOR, ND_BITXOR, ND_SHL, ND_SHR,
   ND_EQ, ND_NE, ND_LT, ND_LE,
   ND_ASSIGN, ND_COND, ND_COMMA, ND_MEMBER, ND_ADDR, ND_DEREF,
   ND_NOT, ND_BITNOT, ND_LOGAND, ND_LOGOR,
   ND_RETURN, ND_IF, ND_FOR, ND_DO, ND_SWITCH, ND_CASE, ND_BLOCK,
   ND_GOTO, ND_LABEL, ND_FUNCALL, ND_EXPR_STMT, ND_VAR, ND_NUM,
   ND_CAST, ND_MEMZERO, ND_ASM
} NodeKind;

struct Node {
   NodeKind kind;
   Node    *next;
   Type    *ty;
   Token   *tok;
   Node    *lhs;
   Node    *rhs;
   /* if / for / while */
   Node    *cond;
   Node    *then;
   Node    *els;
   Node    *init;
   Node    *inc;
   char    *brk_label;
   char    *cont_label;
   Node    *body;
   Member  *member;
   /* funcall */
   Type    *func_ty;
   Node    *args;
   Obj     *ret_buffer;   /* struct return temp */
   /* goto / labels */
   char    *label;
   char    *unique_label;
   Node    *goto_next;
   /* switch */
   Node    *case_next;
   Node    *default_case;
   long     case_val;
   /* variable */
   Obj     *var;
   /* numeric literal */
   long long val;
   double   fval;
   /* asm */
   char    *asm_str;
};

Node *new_cast(Node *expr, Type *ty);
long long const_expr(Token **rest, Token *tok);
Obj  *parse(Token *tok);
extern Obj *all_globals;   /* list of all globals and functions */

/* ---------------- codegen ---------------- */

typedef struct {
   long org;          /* load address of the program */
   long stack_top;    /* initial S */
   char *outname;     /* .x file name */
   int  header;       /* write Sparrow header + BASIC stub */
   int  bss_in_file;
} GenOpts;

void codegen(Obj *prog, FILE *out, GenOpts *opts);
char *new_label(void);

extern int opt_level;

#endif
