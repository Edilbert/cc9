/*
 * stdio for the Sparrow09 console
 * stdin : line buffered, echoed, backspace editing, Ctrl-D = end of file
 * stdout, stderr: unbuffered, written with the BIOS PUTC call
 * There is no file system access yet: fopen() fails with ENOENT.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

int __bios_putc(int c);
int __bios_getc(void);

#define F_EOF   1
#define F_ERR   2
#define F_STR   4

FILE __iob[3] = { { 0, 0, -1, 0 }, { 1, 0, -1, 0 }, { 2, 0, -1, 0 } };

int __stdin_echo = 1;

static char linebuf[BUFSIZ];
static int  line_len, line_pos;

void __stdio_flush(void) { }

static int read_line(void)
{
   int c;
   line_len = line_pos = 0;
   for (;;)
   {
      c = __bios_getc();
      if (c == '\r') c = '\n';
      if (c == 4)
      {
         if (line_len == 0) return EOF;
         continue;
      }
      if (c == 8 || c == 127)
      {
         if (line_len > 0)
         {
            line_len--;
            if (__stdin_echo) { __bios_putc(8); __bios_putc(' '); __bios_putc(8); }
         }
         continue;
      }
      if (c == '\n' || line_len < BUFSIZ - 1)
      {
         linebuf[line_len++] = (char)c;
         if (__stdin_echo) __bios_putc(c);
      }
      if (c == '\n') return 0;
   }
}

int fgetc(FILE *stream)
{
   int c;
   if (stream->ungot >= 0) { c = stream->ungot; stream->ungot = -1; return c; }
   if (stream->flags & F_STR)
   {
      c = (unsigned char)*stream->str;
      if (!c) { stream->flags |= F_EOF; return EOF; }
      stream->str++;
      return c;
   }
   if (stream->fd != 0) { stream->flags |= F_ERR; return EOF; }
   if (stream->flags & F_EOF) return EOF;
   if (line_pos >= line_len)
   {
      if (read_line() == EOF) { stream->flags |= F_EOF; return EOF; }
   }
   return (unsigned char)linebuf[line_pos++];
}

int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *stream)
{
   if (c == EOF || stream->ungot >= 0) return EOF;
   stream->ungot = (unsigned char)c;
   stream->flags &= ~F_EOF;
   return (unsigned char)c;
}

char *fgets(char *s, int n, FILE *stream)
{
   int i = 0, c;
   if (n <= 0) return NULL;
   while (i < n - 1)
   {
      c = fgetc(stream);
      if (c == EOF) break;
      s[i++] = (char)c;
      if (c == '\n') break;
   }
   if (i == 0) return NULL;
   s[i] = 0;
   return s;
}

char *gets(char *s)
{
   int i = 0, c;
   for (;;)
   {
      c = fgetc(stdin);
      if (c == EOF) { if (i == 0) return NULL; break; }
      if (c == '\n') break;
      s[i++] = (char)c;
   }
   s[i] = 0;
   return s;
}

int fputc(int c, FILE *stream)
{
   if (stream->flags & F_STR) { *stream->str++ = (char)c; return (unsigned char)c; }
   if (stream->fd != 1 && stream->fd != 2) { stream->flags |= F_ERR; return EOF; }
   __bios_putc(c & 255);
   return (unsigned char)c;
}

int putc(int c, FILE *stream) { return fputc(c, stream); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *stream)
{
   while (*s) if (fputc(*s++, stream) == EOF) return EOF;
   return 0;
}

int puts(const char *s)
{
   if (fputs(s, stdout) == EOF) return EOF;
   return fputc('\n', stdout) == EOF ? EOF : 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   char *p = ptr;
   size_t i, j;
   for (i = 0; i < nmemb; i++)
      for (j = 0; j < size; j++)
      {
         int c = fgetc(stream);
         if (c == EOF) return i;
         *p++ = (char)c;
      }
   return nmemb;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
   const char *p = ptr;
   size_t i, j;
   for (i = 0; i < nmemb; i++)
      for (j = 0; j < size; j++)
         if (fputc(*p++, stream) == EOF) return i;
   return nmemb;
}

int fflush(FILE *stream) { (void)stream; return 0; }

FILE *fopen(const char *filename, const char *mode)
{
   (void)filename; (void)mode;
   errno = ENOENT;
   return NULL;
}

FILE *freopen(const char *filename, const char *mode, FILE *stream)
{
   (void)filename; (void)mode; (void)stream;
   errno = ENOENT;
   return NULL;
}

int fclose(FILE *stream) { (void)stream; return 0; }
int remove(const char *filename) { (void)filename; errno = ENOENT; return -1; }
int rename(const char *old, const char *new_) { (void)old; (void)new_; errno = ENOENT; return -1; }
FILE *tmpfile(void) { errno = ENOSYS; return NULL; }

char *tmpnam(char *s)
{
   static char name[L_tmpnam] = "TMP0000";
   if (s) { strcpy(s, name); return s; }
   return name;
}

void setbuf(FILE *stream, char *buf) { (void)stream; (void)buf; }

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
   (void)stream; (void)buf; (void)mode; (void)size;
   return 0;
}

int fgetpos(FILE *stream, fpos_t *pos) { (void)stream; *pos = 0; errno = EINVAL; return -1; }
int fsetpos(FILE *stream, const fpos_t *pos) { (void)stream; (void)pos; errno = EINVAL; return -1; }
int fseek(FILE *stream, long offset, int whence) { (void)stream; (void)offset; (void)whence; errno = EINVAL; return -1; }
long ftell(FILE *stream) { (void)stream; errno = EINVAL; return -1L; }
void rewind(FILE *stream) { stream->flags &= ~(F_EOF | F_ERR); }
void clearerr(FILE *stream) { stream->flags &= ~(F_EOF | F_ERR); }
int feof(FILE *stream) { return (stream->flags & F_EOF) != 0; }
int ferror(FILE *stream) { return (stream->flags & F_ERR) != 0; }

void perror(const char *s)
{
   if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
   fputs(strerror(errno), stderr);
   fputc('\n', stderr);
}
