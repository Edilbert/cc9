/*
 * C89 preprocessor (hideset based macro expansion, after Prosser / chibicc)
 */
#include "cc9.h"
#include <time.h>
#include <sys/stat.h>

Vec include_paths;

typedef struct MacroParam { struct MacroParam *next; char *name; } MacroParam;
typedef struct MacroArg   { struct MacroArg *next; char *name; Token *tok; } MacroArg;
typedef Token *macro_handler_fn(Token *);

typedef struct Macro {
   char       *name;
   int         is_objlike;
   MacroParam *params;
   int         is_variadic;
   Token      *body;
   macro_handler_fn *handler;
} Macro;

typedef struct CondIncl {
   struct CondIncl *next;
   enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
   Token *tok;
   int    included;
} CondIncl;

struct Hideset { Hideset *next; char *name; };

static HashMap   macros;
typedef struct MacroStack { struct MacroStack *next; char *name; Macro *macro; } MacroStack;
static MacroStack *macro_stack;
static CondIncl *cond_incl;
static HashMap   pragma_once;

static Token *preprocess2(Token *tok);
static Macro *find_macro(Token *tok);

static int is_hash(Token *tok) { return tok->at_bol && equal(tok, "#"); }

static Token *skip_line(Token *tok)
{
   if (tok->at_bol) return tok;
   warn_tok(tok, "extra token");
   while (!tok->at_bol) tok = tok->next;
   return tok;
}

static Token *copy_token(Token *tok)
{
   Token *t = xmalloc(sizeof(Token));
   *t = *tok;
   t->next = NULL;
   return t;
}

static Token *new_eof(Token *tok)
{
   Token *t = copy_token(tok);
   t->kind = TK_EOF;
   t->len = 0;
   return t;
}

static Hideset *new_hideset(char *name)
{
   Hideset *hs = xcalloc(sizeof(Hideset));
   hs->name = name;
   return hs;
}

static Hideset *hideset_union(Hideset *hs1, Hideset *hs2)
{
   Hideset head, *cur = &head;
   head.next = NULL;
   for (; hs1; hs1 = hs1->next) cur = cur->next = new_hideset(hs1->name);
   cur->next = hs2;
   return head.next;
}

static int hideset_contains(Hideset *hs, char *s, int len)
{
   for (; hs; hs = hs->next)
      if ((int)strlen(hs->name) == len && !strncmp(hs->name, s, len)) return 1;
   return 0;
}

static Hideset *hideset_intersection(Hideset *hs1, Hideset *hs2)
{
   Hideset head, *cur = &head;
   head.next = NULL;
   for (; hs1; hs1 = hs1->next)
      if (hideset_contains(hs2, hs1->name, strlen(hs1->name)))
         cur = cur->next = new_hideset(hs1->name);
   return head.next;
}

static Token *add_hideset(Token *tok, Hideset *hs)
{
   Token head, *cur = &head;
   head.next = NULL;
   for (; tok; tok = tok->next)
   {
      Token *t = copy_token(tok);
      t->hideset = hideset_union(t->hideset, hs);
      cur = cur->next = t;
   }
   return head.next;
}

static Token *append(Token *tok1, Token *tok2)
{
   Token head, *cur = &head;
   if (tok1->kind == TK_EOF) return tok2;
   head.next = NULL;
   for (; tok1->kind != TK_EOF; tok1 = tok1->next) cur = cur->next = copy_token(tok1);
   cur->next = tok2;
   return head.next;
}

static Token *skip_cond_incl2(Token *tok)
{
   while (tok->kind != TK_EOF)
   {
      if (is_hash(tok) && (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
                           equal(tok->next, "ifndef")))
      {
         tok = skip_cond_incl2(tok->next->next);
         continue;
      }
      if (is_hash(tok) && equal(tok->next, "endif")) return tok->next->next;
      tok = tok->next;
   }
   return tok;
}

static Token *skip_cond_incl(Token *tok)
{
   while (tok->kind != TK_EOF)
   {
      if (is_hash(tok) && (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
                           equal(tok->next, "ifndef")))
      {
         tok = skip_cond_incl2(tok->next->next);
         continue;
      }
      if (is_hash(tok) && (equal(tok->next, "elif") || equal(tok->next, "else") ||
                           equal(tok->next, "endif")))
         break;
      tok = tok->next;
   }
   return tok;
}

static char *quote_string(char *str)
{
   int bufsize = 3, i;
   char *buf, *p;
   for (i = 0; str[i]; i++) { if (str[i] == '\\' || str[i] == '"') bufsize++; bufsize++; }
   buf = xcalloc(bufsize);
   p = buf;
   *p++ = '"';
   for (i = 0; str[i]; i++)
   {
      if (str[i] == '\\' || str[i] == '"') *p++ = '\\';
      *p++ = str[i];
   }
   *p++ = '"';
   *p = 0;
   return buf;
}

static Token *new_str_token(char *str, Token *tmpl)
{
   char *buf = quote_string(str);
   return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

static Token *copy_line(Token **rest, Token *tok)
{
   Token head, *cur = &head;
   head.next = NULL;
   for (; !tok->at_bol; tok = tok->next) cur = cur->next = copy_token(tok);
   cur->next = new_eof(tok);
   *rest = tok;
   return head.next;
}

static Token *new_num_token(long val, Token *tmpl)
{
   char *buf = format("%ld\n", val);
   return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

static Token *read_const_expr(Token **rest, Token *tok)
{
   Token head, *cur = &head;
   head.next = NULL;
   tok = copy_line(rest, tok);
   while (tok->kind != TK_EOF)
   {
      if (equal(tok, "defined"))
      {
         Token *start = tok;
         int has_paren = consume(&tok, tok->next, "(");
         Macro *m;
         if (tok->kind != TK_IDENT) error_tok(start, "macro name must be an identifier");
         m = find_macro(tok);
         tok = tok->next;
         if (has_paren) tok = skip(tok, ")");
         cur = cur->next = new_num_token(m ? 1 : 0, start);
         continue;
      }
      cur = cur->next = tok;
      tok = tok->next;
   }
   cur->next = tok;
   return head.next;
}

static long eval_const_expr(Token **rest, Token *tok)
{
   Token *start = tok, *expr, *t, *rest2;
   long val;
   expr = read_const_expr(rest, tok->next);
   expr = preprocess2(expr);
   if (expr->kind == TK_EOF) error_tok(start, "no expression");
   for (t = expr; t->kind != TK_EOF; t = t->next)
   {
      if (t->kind == TK_IDENT)
      {
         Token *next = t->next;
         *t = *new_num_token(0, t);
         t->next = next;
      }
   }
   convert_pp_tokens(expr);
   val = (long)const_expr(&rest2, expr);
   if (rest2->kind != TK_EOF) error_tok(rest2, "extra token");
   return val;
}

static CondIncl *push_cond_incl(Token *tok, int included)
{
   CondIncl *ci = xcalloc(sizeof(CondIncl));
   ci->next = cond_incl;
   ci->ctx = IN_THEN;
   ci->tok = tok;
   ci->included = included;
   cond_incl = ci;
   return ci;
}

static Macro *find_macro(Token *tok)
{
   if (tok->kind != TK_IDENT) return NULL;
   return hm_get2(&macros, tok->loc, tok->len);
}

static Macro *add_macro(char *name, int is_objlike, Token *body)
{
   Macro *m = xcalloc(sizeof(Macro));
   m->name = name;
   m->is_objlike = is_objlike;
   m->body = body;
   hm_put(&macros, name, m);
   return m;
}

static MacroParam *read_macro_params(Token **rest, Token *tok, int *is_variadic)
{
   MacroParam head, *cur = &head;
   head.next = NULL;
   while (!equal(tok, ")"))
   {
      MacroParam *m;
      if (cur != &head) tok = skip(tok, ",");
      if (equal(tok, "..."))
      {
         *is_variadic = 1;
         *rest = skip(tok->next, ")");
         return head.next;
      }
      if (tok->kind != TK_IDENT) error_tok(tok, "expected an identifier");
      m = xcalloc(sizeof(MacroParam));
      m->name = tok_str(tok);
      cur = cur->next = m;
      tok = tok->next;
   }
   *rest = tok->next;
   return head.next;
}

static void read_macro_definition(Token **rest, Token *tok)
{
   char *name;
   if (tok->kind != TK_IDENT) error_tok(tok, "macro name must be an identifier");
   name = tok_str(tok);
   tok = tok->next;
   if (!tok->has_space && equal(tok, "("))
   {
      int is_variadic = 0;
      MacroParam *params = read_macro_params(&tok, tok->next, &is_variadic);
      Macro *m = add_macro(name, 0, copy_line(rest, tok));
      m->params = params;
      m->is_variadic = is_variadic;
   }
   else add_macro(name, 1, copy_line(rest, tok));
}

static MacroArg *read_macro_arg_one(Token **rest, Token *tok, int read_rest)
{
   Token head, *cur = &head;
   int level = 0;
   MacroArg *arg;
   head.next = NULL;
   for (;;)
   {
      if (level == 0 && equal(tok, ")")) break;
      if (level == 0 && !read_rest && equal(tok, ",")) break;
      if (tok->kind == TK_EOF) error_tok(tok, "premature end of input");
      if (equal(tok, "(")) level++;
      else if (equal(tok, ")")) level--;
      cur = cur->next = copy_token(tok);
      tok = tok->next;
   }
   cur->next = new_eof(tok);
   arg = xcalloc(sizeof(MacroArg));
   arg->tok = head.next;
   *rest = tok;
   return arg;
}

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params, int variadic)
{
   Token *start = tok;
   MacroArg head, *cur = &head;
   MacroParam *pp;
   head.next = NULL;
   tok = tok->next->next;
   for (pp = params; pp; pp = pp->next)
   {
      if (cur != &head) tok = skip(tok, ",");
      cur = cur->next = read_macro_arg_one(&tok, tok, 0);
      cur->name = pp->name;
   }
   if (variadic)
   {
      MacroArg *arg;
      if (equal(tok, ")"))
      {
         arg = xcalloc(sizeof(MacroArg));
         arg->tok = new_eof(tok);
      }
      else
      {
         if (pp != params) tok = skip(tok, ",");
         arg = read_macro_arg_one(&tok, tok, 1);
      }
      arg->name = "__VA_ARGS__";
      cur = cur->next = arg;
   }
   else if (!equal(tok, ")")) error_tok(start, "too many arguments");
   *rest = skip(tok, ")");
   return head.next;
}

static MacroArg *find_arg(MacroArg *args, Token *tok)
{
   for (; args; args = args->next)
      if (tok->len == (int)strlen(args->name) && !strncmp(tok->loc, args->name, tok->len))
         return args;
   return NULL;
}

static char *join_tokens(Token *tok, Token *end)
{
   int len = 1;
   Token *t;
   char *buf;
   int pos = 0;
   for (t = tok; t != end && t->kind != TK_EOF; t = t->next)
   {
      if (t != tok && t->has_space) len++;
      len += t->len;
   }
   buf = xcalloc(len);
   for (t = tok; t != end && t->kind != TK_EOF; t = t->next)
   {
      if (t != tok && t->has_space) buf[pos++] = ' ';
      strncpy(buf + pos, t->loc, t->len);
      pos += t->len;
   }
   buf[pos] = 0;
   return buf;
}

static Token *stringize(Token *hash, Token *arg)
{
   char *s = join_tokens(arg, NULL);
   return new_str_token(s, hash);
}

static Token *paste(Token *lhs, Token *rhs)
{
   char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);
   Token *tok = tokenize(new_file(lhs->file->name, lhs->file->file_no, buf));
   if (tok->next->kind != TK_EOF) error_tok(lhs, "pasting forms '%s', an invalid token", buf);
   return tok;
}

static Token *subst(Token *tok, MacroArg *args)
{
   Token head, *cur = &head;
   head.next = NULL;
   while (tok->kind != TK_EOF)
   {
      if (equal(tok, "#"))
      {
         MacroArg *arg = find_arg(args, tok->next);
         if (!arg) error_tok(tok->next, "'#' is not followed by a macro parameter");
         cur = cur->next = stringize(tok, arg->tok);
         tok = tok->next->next;
         continue;
      }
      if (equal(tok, "##"))
      {
         if (cur == &head) error_tok(tok, "'##' cannot appear at start of macro expansion");
         if (tok->next->kind == TK_EOF) error_tok(tok, "'##' cannot appear at end of macro expansion");
         {
            MacroArg *arg = find_arg(args, tok->next);
            if (arg)
            {
               if (arg->tok->kind != TK_EOF)
               {
                  Token *t;
                  *cur = *paste(cur, arg->tok);
                  for (t = arg->tok->next; t->kind != TK_EOF; t = t->next)
                     cur = cur->next = copy_token(t);
               }
               tok = tok->next->next;
               continue;
            }
         }
         *cur = *paste(cur, tok->next);
         tok = tok->next->next;
         continue;
      }
      {
         MacroArg *arg = find_arg(args, tok);
         if (arg && equal(tok->next, "##"))
         {
            Token *rhs = tok->next->next;
            if (arg->tok->kind == TK_EOF)
            {
               MacroArg *arg2 = find_arg(args, rhs);
               if (arg2)
               {
                  Token *t;
                  for (t = arg2->tok; t->kind != TK_EOF; t = t->next)
                     cur = cur->next = copy_token(t);
               }
               else cur = cur->next = copy_token(rhs);
               tok = rhs->next;
               continue;
            }
            {
               Token *t;
               for (t = arg->tok; t->kind != TK_EOF; t = t->next)
                  cur = cur->next = copy_token(t);
            }
            tok = tok->next;
            continue;
         }
         if (arg)
         {
            Token *t = preprocess2(arg->tok);
            t->at_bol = tok->at_bol;
            t->has_space = tok->has_space;
            for (; t->kind != TK_EOF; t = t->next) cur = cur->next = copy_token(t);
            tok = tok->next;
            continue;
         }
      }
      cur = cur->next = copy_token(tok);
      tok = tok->next;
   }
   cur->next = tok;
   return head.next;
}

static int expand_macro(Token **rest, Token *tok)
{
   Macro *m;
   if (hideset_contains(tok->hideset, tok->loc, tok->len)) return 0;
   m = find_macro(tok);
   if (!m) return 0;
   if (m->handler)
   {
      *rest = m->handler(tok);
      (*rest)->next = tok->next;
      return 1;
   }
   if (m->is_objlike)
   {
      Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
      Token *body = add_hideset(m->body, hs), *t;
      for (t = body; t->kind != TK_EOF; t = t->next) t->origin = tok;
      *rest = append(body, tok->next);
      (*rest)->at_bol = tok->at_bol;
      (*rest)->has_space = tok->has_space;
      return 1;
   }
   if (!equal(tok->next, "(")) return 0;
   {
      Token *macro_token = tok, *rparen, *body, *t;
      MacroArg *args = read_macro_args(&tok, tok, m->params, m->is_variadic);
      Hideset *hs;
      rparen = tok;
      /* tok now points after ')', find the ')' token for hideset */
      {
         Token *p = macro_token;
         rparen = NULL;
         while (p != tok) { rparen = p; p = p->next; }
      }
      hs = hideset_intersection(macro_token->hideset, rparen->hideset);
      hs = hideset_union(hs, new_hideset(m->name));
      body = subst(m->body, args);
      body = add_hideset(body, hs);
      for (t = body; t->kind != TK_EOF; t = t->next) t->origin = macro_token;
      *rest = append(body, tok);
      (*rest)->at_bol = macro_token->at_bol;
      (*rest)->has_space = macro_token->has_space;
   }
   return 1;
}

static int file_exists(char *path)
{
   struct stat st;
   return !stat(path, &st);
}

static char *search_include_paths(char *filename)
{
   int i;
   if (filename[0] == '/') return filename;
   for (i = 0; i < include_paths.len; i++)
   {
      char *path = format("%s/%s", (char *)include_paths.data[i], filename);
      if (file_exists(path)) return path;
   }
   return NULL;
}

static char *read_include_filename(Token **rest, Token *tok, int *is_dquote)
{
   if (tok->kind == TK_STR)
   {
      *is_dquote = 1;
      *rest = skip_line(tok->next);
      return xstrndup(tok->loc + 1, tok->len - 2);
   }
   if (equal(tok, "<"))
   {
      Token *start = tok;
      for (; !equal(tok, ">"); tok = tok->next)
         if (tok->at_bol || tok->kind == TK_EOF) error_tok(tok, "expected '>'");
      *is_dquote = 0;
      *rest = skip_line(tok->next);
      return join_tokens(start->next, tok);
   }
   if (tok->kind == TK_IDENT)
   {
      Token *tok2 = preprocess2(copy_line(rest, tok));
      Token *dummy;
      return read_include_filename(&dummy, tok2, is_dquote);
   }
   error_tok(tok, "expected a filename");
   return NULL;
}

static Token *include_file(Token *tok, char *path, Token *filename_tok)
{
   Token *tok2;
   if (hm_get(&pragma_once, path)) return tok;
   tok2 = tokenize_file(path);
   if (!tok2) error_tok(filename_tok, "%s: cannot open file", path);
   return append(tok2, tok);
}

static void read_line_marker(Token **rest, Token *tok)
{
   Token *start = tok;
   tok = preprocess2(copy_line(rest, tok));
   convert_pp_tokens(tok);
   if (tok->kind != TK_NUM || tok->ty->kind == TY_DOUBLE) error_tok(tok, "invalid line marker");
   start->file->line_delta = (int)(tok->val - (start->line_no + 1));
   tok = tok->next;
   if (tok->kind == TK_EOF) return;
   if (tok->kind != TK_STR) error_tok(tok, "filename expected");
   start->file->display_name = tok->str;
}

static Token *preprocess2(Token *tok)
{
   Token head, *cur = &head;
   head.next = NULL;
   while (tok->kind != TK_EOF)
   {
      Token *start;
      if (expand_macro(&tok, tok)) continue;
      if (!is_hash(tok))
      {
         tok->line_no += tok->file->line_delta;
         tok->filename = tok->file->display_name;
         cur = cur->next = tok;
         tok = tok->next;
         continue;
      }
      start = tok;
      tok = tok->next;

      if (equal(tok, "include"))
      {
         int is_dquote;
         char *filename = read_include_filename(&tok, tok->next, &is_dquote);
         char *path = NULL;
         if (filename[0] != '/' && is_dquote)
         {
            char *dir = xstrdup(start->file->name), *slash = strrchr(dir, '/');
            if (slash) { *slash = 0; path = format("%s/%s", dir, filename); }
            else path = filename;
            if (!file_exists(path)) path = NULL;
         }
         if (!path) path = search_include_paths(filename);
         if (!path) error_tok(start->next, "cannot find include file %s", filename);
         tok = include_file(tok, path, start->next->next);
         continue;
      }
      if (equal(tok, "define")) { read_macro_definition(&tok, tok->next); continue; }
      if (equal(tok, "undef"))
      {
         tok = tok->next;
         if (tok->kind != TK_IDENT) error_tok(tok, "macro name must be an identifier");
         undef_macro(tok_str(tok));
         tok = skip_line(tok->next);
         continue;
      }
      if (equal(tok, "if"))
      {
         long val = eval_const_expr(&tok, tok);
         push_cond_incl(start, val != 0);
         if (!val) tok = skip_cond_incl(tok);
         continue;
      }
      if (equal(tok, "ifdef"))
      {
         int defined = find_macro(tok->next) != NULL;
         push_cond_incl(tok, defined);
         tok = skip_line(tok->next->next);
         if (!defined) tok = skip_cond_incl(tok);
         continue;
      }
      if (equal(tok, "ifndef"))
      {
         int defined = find_macro(tok->next) != NULL;
         push_cond_incl(tok, !defined);
         tok = skip_line(tok->next->next);
         if (defined) tok = skip_cond_incl(tok);
         continue;
      }
      if (equal(tok, "elif"))
      {
         if (!cond_incl || cond_incl->ctx == IN_ELSE) error_tok(start, "stray #elif");
         cond_incl->ctx = IN_ELIF;
         if (!cond_incl->included && eval_const_expr(&tok, tok)) cond_incl->included = 1;
         else tok = skip_cond_incl(tok);
         continue;
      }
      if (equal(tok, "else"))
      {
         if (!cond_incl || cond_incl->ctx == IN_ELSE) error_tok(start, "stray #else");
         cond_incl->ctx = IN_ELSE;
         tok = skip_line(tok->next);
         if (cond_incl->included) tok = skip_cond_incl(tok);
         continue;
      }
      if (equal(tok, "endif"))
      {
         if (!cond_incl) error_tok(start, "stray #endif");
         cond_incl = cond_incl->next;
         tok = skip_line(tok->next);
         continue;
      }
      if (equal(tok, "line")) { read_line_marker(&tok, tok->next); continue; }
      if (tok->kind == TK_PPNUM) { read_line_marker(&tok, tok); continue; }
      if (equal(tok, "pragma") && equal(tok->next, "once"))
      {
         hm_put(&pragma_once, tok->file->name, (void *)1);
         tok = skip_line(tok->next->next);
         continue;
      }
      if (equal(tok, "pragma") && (equal(tok->next, "push_macro") || equal(tok->next, "pop_macro")) &&
          equal(tok->next->next, "(") && tok->next->next->next->kind == TK_STR)
      {
         int push = equal(tok->next, "push_macro");
         Token *nt = tok->next->next->next;
         char *name = xstrndup(nt->loc + 1, nt->len - 2);
         MacroStack *ms;
         if (push)
         {
            ms = xcalloc(sizeof(MacroStack));
            ms->name = name;
            ms->macro = hm_get(&macros, name);
            ms->next = macro_stack;
            macro_stack = ms;
         }
         else
         {
            MacroStack **pp;
            for (pp = &macro_stack; *pp; pp = &(*pp)->next)
               if (!strcmp((*pp)->name, name))
               {
                  ms = *pp;
                  *pp = ms->next;
                  if (ms->macro) hm_put(&macros, name, ms->macro);
                  else hm_del(&macros, name);
                  break;
               }
         }
         do { tok = tok->next; } while (!tok->at_bol);
         continue;
      }
      if (equal(tok, "pragma"))
      {
         do { tok = tok->next; } while (!tok->at_bol);
         continue;
      }
      if (equal(tok, "error"))
      {
         Token *e = tok->next;
         while (!e->at_bol && e->kind != TK_EOF) e = e->next;
         error_tok(tok, "#error %s", join_tokens(tok->next, e));
      }
      if (equal(tok, "warning"))
      {
         warn_tok(tok, "#warning");
         do { tok = tok->next; } while (!tok->at_bol);
         continue;
      }
      if (tok->at_bol) continue;   /* null directive */
      error_tok(tok, "invalid preprocessor directive");
   }
   cur->next = tok;
   return head.next;
}

void define_macro(char *name, char *buf)
{
   Token *tok = tokenize(new_file("<built-in>", 1, buf));
   add_macro(name, 1, tok);
}

void undef_macro(char *name) { hm_del(&macros, name); }

static Macro *add_builtin(char *name, macro_handler_fn *fn)
{
   Macro *m = add_macro(name, 1, NULL);
   m->handler = fn;
   return m;
}

static Token *file_macro(Token *tmpl)
{
   while (tmpl->origin) tmpl = tmpl->origin;
   return new_str_token(tmpl->file->display_name, tmpl);
}

static Token *line_macro(Token *tmpl)
{
   int i;
   while (tmpl->origin) tmpl = tmpl->origin;
   i = tmpl->line_no + tmpl->file->line_delta;
   return new_num_token(i, tmpl);
}

static char *format_date(struct tm *tm)
{
   static char mon[][4] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug",
                            "Sep","Oct","Nov","Dec" };
   return format("\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
}

static char *format_time(struct tm *tm)
{
   return format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void init_macros(void)
{
   time_t now = time(NULL);
   struct tm *tm = localtime(&now);
   define_macro("__STDC__", "1");
   define_macro("__CC9__", "1");
   define_macro("__SPARROW09__", "1");
   define_macro("__6309__", "1");
   define_macro("__6809__", "1");
   define_macro("__BIG_ENDIAN__", "1");
   if (!is_char_signed) define_macro("__CHAR_UNSIGNED__", "1");
   add_builtin("__FILE__", file_macro);
   add_builtin("__LINE__", line_macro);
   define_macro("__DATE__", format_date(tm));
   define_macro("__TIME__", format_time(tm));
}

static void join_adjacent_string_literals(Token *tok)
{
   Token *tok1;
   for (tok1 = tok; tok1->kind != TK_EOF;)
   {
      Token *tok2;
      int len, wide;
      char *buf;
      if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) { tok1 = tok1->next; continue; }
      wide = tok1->ty->base->size == 2;
      tok2 = tok1->next;
      while (tok2->kind == TK_STR) tok2 = tok2->next;
      len = tok1->strlen;
      {
         Token *t;
         int esz = wide ? 2 : 1;
         for (t = tok1->next; t != tok2; t = t->next) len += t->strlen - esz;
         buf = xcalloc(len);
         {
            int i = 0;
            for (t = tok1; t != tok2; t = t->next)
            {
               memcpy(buf + i, t->str, t->strlen);
               i += t->strlen - esz;
            }
         }
         *tok1 = *copy_token(tok1);
         tok1->ty = array_of(tok1->ty->base, len / esz);
         tok1->str = buf;
         tok1->strlen = len;
      }
      tok1->next = tok2;
      tok1 = tok2;
   }
}

Token *preprocess(Token *tok)
{
   tok = preprocess2(tok);
   if (cond_incl) error_tok(cond_incl->tok, "unterminated conditional directive");
   convert_pp_tokens(tok);
   join_adjacent_string_literals(tok);
   {
      Token *t;
      for (t = tok; t; t = t->next) t->line_no += 0;
   }
   return tok;
}

void reset_preprocessor(void)
{
   memset(&macros, 0, sizeof(macros));
   memset(&pragma_once, 0, sizeof(pragma_once));
   cond_incl = NULL;
}

/* process a string with #define directives (built-in macros) */
void preprocess_defines(char *src)
{
   preprocess2(tokenize(new_file("<built-in>", 1, src)));
}
