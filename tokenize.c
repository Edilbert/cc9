#include "cc9.h"

static File  *current_file;
static File **input_files;
static int    at_bol;
static int    has_space;

static const char *keywords[] = {
   "auto", "break", "case", "char", "const", "continue", "default", "do",
   "double", "else", "enum", "extern", "float", "for", "goto", "if", "int",
   "long", "register", "return", "short", "signed", "sizeof", "static",
   "struct", "switch", "typedef", "union", "unsigned", "void", "volatile",
   "while", "asm", "__asm", "__asm__", "__naked", "__inline", "inline",
   "__signed__", "__const", "__volatile__", NULL
};

int equal(Token *tok, const char *op)
{
   return (int)strlen(op) == tok->len && !memcmp(tok->loc, op, tok->len);
}

Token *skip(Token *tok, const char *op)
{
   if (!equal(tok, op)) error_tok(tok, "expected '%s'", op);
   return tok->next;
}

int consume(Token **rest, Token *tok, const char *str)
{
   if (equal(tok, str)) { *rest = tok->next; return 1; }
   *rest = tok;
   return 0;
}

char *tok_str(Token *tok) { return xstrndup(tok->loc, tok->len); }

static Token *new_token(TokenKind kind, char *start, char *end)
{
   Token *tok = xcalloc(sizeof(Token));
   tok->kind = kind;
   tok->loc = start;
   tok->len = (int)(end - start);
   tok->file = current_file;
   tok->filename = current_file->display_name;
   tok->at_bol = at_bol;
   tok->has_space = has_space;
   at_bol = has_space = 0;
   return tok;
}

static int startswith(char *p, const char *q) { return !strncmp(p, q, strlen(q)); }

static int is_ident1(int c) { return isalpha(c) || c == '_' || c == '$'; }
static int is_ident2(int c) { return isalnum(c) || c == '_' || c == '$'; }

static int read_punct(char *p)
{
   static const char *kw[] = {
      "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=", "-=", "*=",
      "/=", "++", "--", "%=", "&=", "|=", "^=", "&&", "||", "<<", ">>", "##",
      NULL };
   int i;
   for (i = 0; kw[i]; i++) if (startswith(p, kw[i])) return strlen(kw[i]);
   return ispunct((unsigned char)*p) ? 1 : 0;
}

static int is_keyword(Token *tok)
{
   static HashMap map;
   int i;
   if (map.capacity == 0)
      for (i = 0; keywords[i]; i++) hm_put(&map, keywords[i], (void *)1);
   return hm_get2(&map, tok->loc, tok->len) != NULL;
}

static int from_hex(int c)
{
   if ('0' <= c && c <= '9') return c - '0';
   if ('a' <= c && c <= 'f') return c - 'a' + 10;
   return c - 'A' + 10;
}

static int read_escaped_char(Token *tk, char **new_pos, char *p)
{
   int c;
   if ('0' <= *p && *p <= '7')
   {
      c = *p++ - '0';
      if ('0' <= *p && *p <= '7')
      {
         c = (c << 3) + (*p++ - '0');
         if ('0' <= *p && *p <= '7') c = (c << 3) + (*p++ - '0');
      }
      *new_pos = p;
      return c;
   }
   if (*p == 'x')
   {
      p++;
      if (!isxdigit((unsigned char)*p)) error_tok(tk, "invalid hex escape sequence");
      for (c = 0; isxdigit((unsigned char)*p); p++) c = (c << 4) + from_hex(*p);
      *new_pos = p;
      return c;
   }
   *new_pos = p + 1;
   switch (*p)
   {
      case 'a': return 7;
      case 'b': return 8;
      case 't': return 9;
      case 'n': return 10;
      case 'v': return 11;
      case 'f': return 12;
      case 'r': return 13;
      case 'e': return 27;
      default:  return *p;
   }
}

static char *string_literal_end(Token *tk, char *p)
{
   char *start = p;
   for (; *p != '"'; p++)
   {
      if (*p == '\n' || *p == '\0')
      {
         tk->loc = start;
         error_tok(tk, "unclosed string literal");
      }
      if (*p == '\\') p++;
   }
   return p;
}

static Token *read_string_literal(char *start, char *quote, int wide)
{
   Token dummy;
   char *end, *buf, *p;
   int len = 0, i, n;
   Token *tok;
   dummy.file = current_file; dummy.filename = current_file->display_name;
   dummy.loc = start; dummy.line_no = 0;
   end = string_literal_end(&dummy, quote + 1);
   buf = xcalloc(end - quote);
   for (p = quote + 1; p < end;)
   {
      if (*p == '\\') buf[len++] = read_escaped_char(&dummy, &p, p + 1);
      else buf[len++] = *p++;
   }
   tok = new_token(TK_STR, start, end + 1);
   if (wide)
   {
      /* wchar_t = 16 bit, big endian */
      n = len + 1;
      tok->str = xcalloc(n * 2);
      for (i = 0; i < len; i++) { tok->str[2*i] = 0; tok->str[2*i+1] = buf[i]; }
      tok->strlen = n * 2;
      tok->ty = array_of(ty_int, n);
   }
   else
   {
      tok->ty = array_of(ty_char, len + 1);
      tok->str = buf;
      tok->strlen = len + 1;
   }
   return tok;
}

static Token *read_char_literal(char *start, char *quote)
{
   Token dummy, *tok;
   char *p = quote + 1;
   long c = 0;
   int n = 0, ch;
   dummy.file = current_file; dummy.filename = current_file->display_name;
   dummy.loc = start; dummy.line_no = 0;
   if (*p == '\0') error_tok(&dummy, "unclosed char literal");
   while (*p != '\'')
   {
      if (*p == '\n' || *p == 0) error_tok(&dummy, "unclosed char literal");
      if (*p == '\\') ch = read_escaped_char(&dummy, &p, p + 1);
      else ch = (unsigned char)*p++;
      c = (c << 8) | (ch & 0xff);
      n++;
   }
   tok = new_token(TK_NUM, start, p + 1);
   if (n == 1)
   {
      /* plain char is signed: '\xff' == -1 */
      tok->val = is_char_signed ? (signed char)c : (unsigned char)c;
   }
   else tok->val = (short)c;
   tok->ty = ty_int;
   return tok;
}

static int convert_pp_int(Token *tok)
{
   char *p = tok->loc;
   int base = 10, l = 0, u = 0;
   unsigned long long val;
   Type *ty;
   if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) { p += 2; base = 16; }
   else if (*p == '0') base = 8;
   val = strtoull(p, &p, base);
   for (;;)
   {
      if (*p == 'l' || *p == 'L') { l = 1; p++; }
      else if (*p == 'u' || *p == 'U') { u = 1; p++; }
      else break;
   }
   if (p != tok->loc + tok->len) return 0;
   if (val > 0xffffffffULL) warn_tok(tok, "integer constant too large");
   val &= 0xffffffffULL;
   if (base == 10)
   {
      if (l && u) ty = ty_ulong;
      else if (l) ty = val > 0x7fffffff ? ty_ulong : ty_long;
      else if (u) ty = val > 0xffff ? ty_ulong : ty_uint;
      else if (val <= 0x7fff) ty = ty_int;
      else if (val <= 0x7fffffff) ty = ty_long;
      else ty = ty_ulong;
   }
   else
   {
      if (l && u) ty = ty_ulong;
      else if (l) ty = val > 0x7fffffff ? ty_ulong : ty_long;
      else if (u) ty = val > 0xffff ? ty_ulong : ty_uint;
      else if (val <= 0x7fff) ty = ty_int;
      else if (val <= 0xffff) ty = ty_uint;
      else if (val <= 0x7fffffff) ty = ty_long;
      else ty = ty_ulong;
   }
   tok->kind = TK_NUM;
   if (ty->size == 2) val &= 0xffff;
   if (!ty->is_unsigned)
   {
      if (ty->size == 2) tok->val = (short)val;
      else tok->val = (int32_t)val;
   }
   else tok->val = val;
   tok->ty = ty;
   return 1;
}

static void convert_pp_number(Token *tok)
{
   char *end;
   double val;
   Type *ty = ty_double;
   if (convert_pp_int(tok)) return;
   val = strtod(tok->loc, &end);
   if (*end == 'f' || *end == 'F') { ty = ty_float; end++; }
   else if (*end == 'l' || *end == 'L') { ty = ty_ldouble; end++; }
   if (tok->loc + tok->len != end) error_tok(tok, "invalid numeric constant");
   tok->kind = TK_NUM;
   tok->fval = val;
   tok->ty = ty;
}

void convert_pp_tokens(Token *tok)
{
   Token *t;
   for (t = tok; t->kind != TK_EOF; t = t->next)
   {
      if (is_keyword(t)) t->kind = TK_KEYWORD;
      else if (t->kind == TK_PPNUM) convert_pp_number(t);
   }
}

static void add_line_numbers(Token *tok)
{
   char *p = current_file->contents;
   int n = 1;
   do {
      if (p == tok->loc) { tok->line_no = n; tok = tok->next; }
      if (*p == '\n') n++;
   } while (*p++);
}

Token *tokenize_string_literal(Token *tok, Type *basety)
{
   (void)basety;
   return tok;
}

Token *tokenize(File *file)
{
   char *p = file->contents;
   Token head, *cur = &head;
   head.next = NULL;
   current_file = file;
   at_bol = 1;
   has_space = 0;

   while (*p)
   {
      if (startswith(p, "//"))
      {
         p += 2;
         while (*p != '\n' && *p) p++;
         has_space = 1;
         continue;
      }
      if (startswith(p, "/*"))
      {
         char *q = strstr(p + 2, "*/");
         if (!q)
         {
            Token d; d.file = file; d.filename = file->display_name; d.loc = p; d.line_no = 0;
            error_tok(&d, "unclosed block comment");
         }
         p = q + 2;
         has_space = 1;
         continue;
      }
      if (*p == '\n') { p++; at_bol = 1; has_space = 0; continue; }
      if (isspace((unsigned char)*p)) { p++; has_space = 1; continue; }

      if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1])))
      {
         char *q = p++;
         for (;;)
         {
            if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1])) p += 2;
            else if (isalnum((unsigned char)*p) || *p == '.' || *p == '_') p++;
            else break;
         }
         cur = cur->next = new_token(TK_PPNUM, q, p);
         continue;
      }
      if (*p == '"')
      {
         cur = cur->next = read_string_literal(p, p, 0);
         p += cur->len;
         continue;
      }
      if (p[0] == 'L' && p[1] == '"')
      {
         cur = cur->next = read_string_literal(p, p + 1, 1);
         p += cur->len;
         continue;
      }
      if (*p == '\'')
      {
         cur = cur->next = read_char_literal(p, p);
         p += cur->len;
         continue;
      }
      if (p[0] == 'L' && p[1] == '\'')
      {
         cur = cur->next = read_char_literal(p, p + 1);
         cur->val = (short)cur->val;
         p += cur->len;
         continue;
      }
      if (is_ident1((unsigned char)*p))
      {
         char *start = p;
         do { p++; } while (is_ident2((unsigned char)*p));
         cur = cur->next = new_token(TK_IDENT, start, p);
         continue;
      }
      {
         int len = read_punct(p);
         if (len)
         {
            cur = cur->next = new_token(TK_PUNCT, p, p + len);
            p += len;
            continue;
         }
      }
      {
         Token d; d.file = file; d.filename = file->display_name; d.loc = p; d.line_no = 0;
         error_tok(&d, "invalid token");
      }
   }
   cur = cur->next = new_token(TK_EOF, p, p);
   add_line_numbers(head.next);
   return head.next;
}

static char *read_file(char *path)
{
   FILE *fp;
   char *buf;
   size_t n = 0, cap = 4096, r;
   fp = fopen(path, "rb");
   if (!fp) return NULL;
   buf = xmalloc(cap);
   while ((r = fread(buf + n, 1, cap - n - 2, fp)) > 0)
   {
      n += r;
      if (cap - n < 4096) { cap *= 2; buf = realloc(buf, cap); }
   }
   fclose(fp);
   if (n == 0 || buf[n - 1] != '\n') buf[n++] = '\n';
   buf[n] = 0;
   return buf;
}

File **get_input_files(void) { return input_files; }

File *new_file(char *name, int file_no, char *contents)
{
   File *file = xcalloc(sizeof(File));
   file->name = name;
   file->display_name = name;
   file->file_no = file_no;
   file->contents = contents;
   return file;
}

/* replace trigraphs and remove backslash-newline */
static void canonicalize(char *p)
{
   char *q = p, *r = p;
   int n = 0;
   /* trigraphs */
   while (*r)
   {
      if (r[0] == '?' && r[1] == '?' && r[2] && strchr("=/'()!<>-", r[2]))
      {
         switch (r[2])
         {
            case '=': *q++ = '#';  break;
            case '/': *q++ = '\\'; break;
            case '\'':*q++ = '^';  break;
            case '(': *q++ = '[';  break;
            case ')': *q++ = ']';  break;
            case '!': *q++ = '|';  break;
            case '<': *q++ = '{';  break;
            case '>': *q++ = '}';  break;
            case '-': *q++ = '~';  break;
         }
         r += 3;
      }
      else *q++ = *r++;
   }
   *q = 0;
   /* line splicing, keep line count by re-adding newlines */
   q = r = p;
   while (*r)
   {
      if (r[0] == '\\' && r[1] == '\n') { r += 2; n++; }
      else if (r[0] == '\\' && r[1] == '\r' && r[2] == '\n') { r += 3; n++; }
      else if (*r == '\r') r++;
      else if (*r == '\n') { *q++ = *r++; for (; n > 0; n--) *q++ = '\n'; }
      else *q++ = *r++;
   }
   for (; n > 0; n--) *q++ = '\n';
   *q = 0;
}

Token *tokenize_file(char *path)
{
   static int file_no;
   char *p = read_file(path);
   File *file;
   if (!p) return NULL;
   canonicalize(p);
   file = new_file(path, file_no + 1, p);
   input_files = realloc(input_files, sizeof(File *) * (file_no + 2));
   input_files[file_no] = file;
   input_files[file_no + 1] = NULL;
   file_no++;
   return tokenize(file);
}
