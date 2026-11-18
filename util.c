#include "cc9.h"

int warnings_on = 1;

void *xmalloc(size_t n)
{
   void *p = malloc(n ? n : 1);
   if (!p) { fprintf(stderr, "cc9: out of memory\n"); exit(1); }
   return p;
}

void *xcalloc(size_t n)
{
   void *p = calloc(1, n ? n : 1);
   if (!p) { fprintf(stderr, "cc9: out of memory\n"); exit(1); }
   return p;
}

char *xstrdup(const char *s)
{
   return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
   char *p = xmalloc(n + 1);
   memcpy(p, s, n);
   p[n] = 0;
   return p;
}

char *format(const char *fmt, ...)
{
   char buf[4096];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   return xstrdup(buf);
}

void error(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   fprintf(stderr, "cc9: error: ");
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
   va_end(ap);
   exit(1);
}

static void verror_at(const char *kind, char *filename, char *input, int line_no,
                      char *loc, const char *fmt, va_list ap)
{
   char *line = loc, *end = loc;
   int indent, pos, i;
   if (!input) input = loc;
   if (line_no <= 0 && input != loc)
   {
      char *q;
      line_no = 1;
      for (q = input; q < loc; q++) if (*q == '\n') line_no++;
   }
   while (input < line && line[-1] != '\n') line--;
   while (*end && *end != '\n') end++;
   indent = fprintf(stderr, "%s:%d: %s: ", filename, line_no, kind);
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");
   fprintf(stderr, "    %.*s\n", (int)(end - line), line);
   pos = (int)(loc - line) + 4;
   for (i = 0; i < pos; i++) fputc((i >= 4 && line[i-4] == '\t') ? '\t' : ' ', stderr);
   fprintf(stderr, "^\n");
   (void)indent;
}

void error_tok(Token *tok, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   verror_at("error", tok->filename ? tok->filename : tok->file->name,
             tok->file ? tok->file->contents : NULL, tok->line_no, tok->loc, fmt, ap);
   va_end(ap);
   exit(1);
}

void warn_tok(Token *tok, const char *fmt, ...)
{
   va_list ap;
   if (!warnings_on) return;
   va_start(ap, fmt);
   verror_at("warning", tok->filename ? tok->filename : tok->file->name,
             tok->file ? tok->file->contents : NULL, tok->line_no, tok->loc, fmt, ap);
   va_end(ap);
}

void vec_push(Vec *v, void *p)
{
   if (v->len == v->cap)
   {
      v->cap = v->cap ? v->cap * 2 : 8;
      v->data = realloc(v->data, sizeof(void *) * v->cap);
   }
   v->data[v->len++] = p;
}

/* ---------------- hash map (open addressing) ---------------- */

#define TOMBSTONE ((void *)-1)

static uint64_t fnv_hash(const char *s, int len)
{
   uint64_t h = 0xcbf29ce484222325ULL;
   int i;
   for (i = 0; i < len; i++) { h *= 0x100000001b3ULL; h ^= (unsigned char)s[i]; }
   return h;
}

static int match(HashEntry *e, const char *key, int keylen)
{
   return e->key && e->key != TOMBSTONE && e->keylen == keylen &&
          !memcmp(e->key, key, keylen);
}

static void rehash(HashMap *m)
{
   int nkeys = 0, i, cap;
   HashMap m2;
   for (i = 0; i < m->capacity; i++)
      if (m->buckets[i].key && m->buckets[i].key != TOMBSTONE) nkeys++;
   cap = m->capacity;
   while ((nkeys * 100) / cap >= 50) cap *= 2;
   m2.buckets = xcalloc(sizeof(HashEntry) * cap);
   m2.capacity = cap;
   m2.used = 0;
   for (i = 0; i < m->capacity; i++)
   {
      HashEntry *e = &m->buckets[i];
      if (e->key && e->key != TOMBSTONE) hm_put2(&m2, e->key, e->keylen, e->val);
   }
   free(m->buckets);
   *m = m2;
}

static HashEntry *get_entry(HashMap *m, const char *key, int keylen)
{
   uint64_t h;
   int i;
   if (!m->buckets) return NULL;
   h = fnv_hash(key, keylen);
   for (i = 0; i < m->capacity; i++)
   {
      HashEntry *e = &m->buckets[(h + i) % m->capacity];
      if (match(e, key, keylen)) return e;
      if (e->key == NULL) return NULL;
   }
   return NULL;
}

static HashEntry *get_or_insert_entry(HashMap *m, const char *key, int keylen)
{
   uint64_t h;
   int i;
   if (!m->buckets)
   {
      m->buckets = xcalloc(sizeof(HashEntry) * 16);
      m->capacity = 16;
   }
   else if ((m->used * 100) / m->capacity >= 70) rehash(m);
   h = fnv_hash(key, keylen);
   for (i = 0; i < m->capacity; i++)
   {
      HashEntry *e = &m->buckets[(h + i) % m->capacity];
      if (match(e, key, keylen)) return e;
      if (e->key == TOMBSTONE) { e->key = (char *)key; e->keylen = keylen; return e; }
      if (e->key == NULL)
      {
         e->key = (char *)key; e->keylen = keylen; m->used++; return e;
      }
   }
   error("hashmap full");
   return NULL;
}

void *hm_get(HashMap *m, const char *key) { return hm_get2(m, key, strlen(key)); }

void *hm_get2(HashMap *m, const char *key, int len)
{
   HashEntry *e = get_entry(m, key, len);
   return e ? e->val : NULL;
}

void hm_put(HashMap *m, const char *key, void *val) { hm_put2(m, key, strlen(key), val); }

void hm_put2(HashMap *m, const char *key, int len, void *val)
{
   HashEntry *e = get_or_insert_entry(m, key, len);
   e->val = val;
}

void hm_del(HashMap *m, const char *key)
{
   HashEntry *e = get_entry(m, key, strlen(key));
   if (e) e->key = TOMBSTONE;
}

/* portable replacement for open_memstream: write to a temporary file,
   mem_close() returns the contents as a string */
FILE *mem_open(void)
{
   FILE *fp = tmpfile();
   if (!fp) error("cannot create temporary file");
   return fp;
}

char *mem_close(FILE *fp)
{
   long n;
   char *buf;
   fflush(fp);
   n = ftell(fp);
   buf = xmalloc(n + 1);
   rewind(fp);
   if (n > 0 && fread(buf, 1, n, fp) != (size_t)n) error("temporary file read error");
   buf[n] = 0;
   fclose(fp);
   return buf;
}
