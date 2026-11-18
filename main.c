/*
 * cc9 driver
 */
#include "cc9.h"
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern int parse_file_no;
void reset_preprocessor(void);
void preprocess_defines(char *src);

static Vec   input_names;
static Vec   cmd_defines;   /* "name=value" or "name" */
static Vec   cmd_undefs;
static Vec   user_includes;
static char *opt_o;
static int   opt_S, opt_E, opt_nostdlib, opt_keep, opt_verbose;
static char *libdir;
static char *bs9_path;
static GenOpts gopts;

static void usage(int status)
{
   fprintf(status ? stderr : stdout,
      "cc9 %s - ANSI C (C89) compiler for the Sparrow09 (6309)\n"
      "\n"
      "usage: cc9 [options] file.c ...\n"
      "\n"
      "  -o file        output file (default: first source with .x)\n"
      "  -S             write assembler source (.as9) only\n"
      "  -E             preprocess only, write to stdout\n"
      "  -I dir         add include directory\n"
      "  -D name[=val]  define macro\n"
      "  -U name        undefine macro\n"
      "  -L dir         library directory (default: <cc9 dir>/lib)\n"
      "  -B path        BS9 assembler executable (default: bs9)\n"
      "  -funsigned-char  plain char is unsigned\n"
      "  -nostdlib      do not use the C library\n"
      "  -w             suppress warnings\n"
      "  -k             keep the .as9 file after assembling\n"
      "  -v             verbose\n"
      "  --org=ADDR     load address (default $2000)\n"
      "  --stack=ADDR   initial stack pointer (default $8000)\n"
      "  --no-header    no Sparrow header / BASIC SYS line\n"
      "\n"
      "Output: Sparrow file, load with LOAD and start with RUN.\n",
      CC9_VERSION);
   exit(status);
}

static long parse_addr(const char *s)
{
   if (*s == '$') return strtol(s + 1, NULL, 16);
   return strtol(s, NULL, 0);
}

static char *exe_dir(char *argv0)
{
   char buf[4096], *p;
   int ok = 0;
   buf[0] = 0;
#if defined(_WIN32)
   ok = GetModuleFileNameA(NULL, buf, sizeof(buf)) > 0;
   for (p = buf; *p; p++) if (*p == '\\') *p = '/';
#elif defined(__APPLE__)
   {
      uint32_t size = sizeof(buf);
      ok = _NSGetExecutablePath(buf, &size) == 0;
   }
#else
   {
      ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
      if (n > 0) { buf[n] = 0; ok = 1; }
   }
#endif
   if (!ok) { strncpy(buf, argv0, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
   p = strrchr(buf, '/');
   if (!p) return xstrdup(".");
   *p = 0;
   return xstrdup(buf);
}

static int dir_exists(const char *p)
{
   struct stat st;
   return !stat(p, &st) && S_ISDIR(st.st_mode);
}

static void parse_args(int argc, char **argv)
{
   int i;
   gopts.org = 0x2000;
   gopts.stack_top = 0x8000;
   gopts.header = 1;
   for (i = 1; i < argc; i++)
   {
      char *a = argv[i];
#define ARG(opt) (!strcmp(a, opt) ? (i + 1 < argc ? argv[++i] : (usage(1), (char *)0)) : a + strlen(opt))
      if (!strcmp(a, "-h") || !strcmp(a, "--help")) usage(0);
      else if (!strcmp(a, "--version")) { printf("cc9 %s\n", CC9_VERSION); exit(0); }
      else if (!strncmp(a, "-o", 2)) opt_o = ARG("-o");
      else if (!strcmp(a, "-S")) opt_S = 1;
      else if (!strcmp(a, "-E")) opt_E = 1;
      else if (!strncmp(a, "-I", 2)) vec_push(&user_includes, ARG("-I"));
      else if (!strncmp(a, "-D", 2)) vec_push(&cmd_defines, ARG("-D"));
      else if (!strncmp(a, "-U", 2)) vec_push(&cmd_undefs, ARG("-U"));
      else if (!strncmp(a, "-L", 2)) libdir = ARG("-L");
      else if (!strncmp(a, "-B", 2)) bs9_path = ARG("-B");
      else if (!strcmp(a, "-funsigned-char")) is_char_signed = 0;
      else if (!strcmp(a, "-fsigned-char")) is_char_signed = 1;
      else if (!strcmp(a, "-nostdlib")) opt_nostdlib = 1;
      else if (!strcmp(a, "-w")) warnings_on = 0;
      else if (!strcmp(a, "-k")) opt_keep = 1;
      else if (!strcmp(a, "-v")) opt_verbose = 1;
      else if (!strncmp(a, "-O", 2)) opt_level = atoi(a + 2);
      else if (!strncmp(a, "--org=", 6)) gopts.org = parse_addr(a + 6);
      else if (!strncmp(a, "--stack=", 8)) gopts.stack_top = parse_addr(a + 8);
      else if (!strcmp(a, "--no-header")) gopts.header = 0;
      else if (a[0] == '-' && a[1]) { fprintf(stderr, "cc9: unknown option %s\n", a); usage(1); }
      else vec_push(&input_names, a);
#undef ARG
   }
   if (input_names.len == 0) usage(1);
}

static void setup_macros(void)
{
   int i;
   reset_preprocessor();
   init_macros();
   preprocess_defines("#define __attribute__(x)\n#define __extension__\n"
                      "#define __restrict\n#define __inline__ inline\n");
   for (i = 0; i < cmd_defines.len; i++)
   {
      char *d = xstrdup(cmd_defines.data[i]);
      char *eq = strchr(d, '=');
      if (eq) { *eq = 0; define_macro(d, eq + 1); }
      else define_macro(d, "1");
   }
   for (i = 0; i < cmd_undefs.len; i++) undef_macro(cmd_undefs.data[i]);
}

static Obj *compile_file(char *path, int file_no)
{
   Token *tok;
   setup_macros();
   tok = tokenize_file(path);
   if (!tok) error("cannot open %s", path);
   tok = preprocess(tok);
   if (opt_E)
   {
      int line = 0;
      Token *t;
      for (t = tok; t->kind != TK_EOF; t = t->next)
      {
         if (line && t->at_bol) printf("\n");
         if (t->has_space && !t->at_bol) printf(" ");
         printf("%.*s", t->len, t->loc);
         line = 1;
      }
      printf("\n");
      return NULL;
   }
   parse_file_no = file_no;
   return parse(tok);
}

static char *replace_ext(const char *path, const char *ext)
{
   char *base = xstrdup(path), *dot = strrchr(base, '.'), *slash = strrchr(base, '/');
   if (dot && (!slash || dot > slash)) *dot = 0;
   return format("%s%s", base, ext);
}

static int cmp_str(const void *a, const void *b)
{
   return strcmp(*(char **)a, *(char **)b);
}

/* lib/ *.as9: assembler units, separated by lines starting with "*=====" */
static void add_asm_units(const char *dirname, Obj ***tailp)
{
   DIR *d = opendir(dirname);
   struct dirent *de;
   Vec files = {0};
   int i;
   if (!d) return;
   while ((de = readdir(d)))
   {
      int n = strlen(de->d_name);
      if (n > 4 && !strcmp(de->d_name + n - 4, ".as9"))
         vec_push(&files, format("%s/%s", dirname, de->d_name));
   }
   closedir(d);
   qsort(files.data, files.len, sizeof(char *), cmp_str);
   for (i = 0; i < files.len; i++)
   {
      FILE *fp = fopen(files.data[i], "r");
      char line[1024];
      FILE *ms = NULL;
      if (!fp) continue;
      for (;;)
      {
         int eof = !fgets(line, sizeof(line), fp);
         if (eof || !strncmp(line, "*=====", 6))
         {
            if (ms)
            {
               Obj *o;
               char *buf = mem_close(ms);
               o = xcalloc(sizeof(Obj));
               o->name = "asm";
               o->ty = ty_void;
               o->asmtext = buf;
               **tailp = o;
               *tailp = &o->next;
               ms = NULL;
            }
            if (eof) break;
            continue;
         }
         if (!ms) ms = mem_open();
         fputs(line, ms);
      }
      fclose(fp);
   }
}

int main(int argc, char **argv)
{
   int i, file_no = 1;
   Obj *prog = NULL, **tail = &prog;
   char *dir, *incdir, *asmname, *outname;
   FILE *fp;

   parse_args(argc, argv);
   ty_char->is_unsigned = !is_char_signed;
   dir = exe_dir(argv[0]);
   if (!libdir)
   {
      libdir = format("%s/lib", dir);
      if (!dir_exists(libdir)) libdir = format("%s/../lib/cc9/lib", dir);
   }
   incdir = format("%s/../include", libdir);
   if (!dir_exists(incdir)) incdir = format("%s/include", dir);
   for (i = 0; i < user_includes.len; i++) vec_push(&include_paths, user_includes.data[i]);
   vec_push(&include_paths, incdir);

   if (!bs9_path)
   {
      char *p = format("%s/bs9", dir);
      bs9_path = access(p, X_OK) == 0 ? p : "bs9";
   }

   outname = opt_o ? opt_o : replace_ext(input_names.data[0], ".x");
   asmname = opt_S ? (opt_o ? opt_o : replace_ext(input_names.data[0], ".as9"))
                   : replace_ext(outname, ".as9");

   for (i = 0; i < input_names.len; i++)
   {
      Obj *o = compile_file(input_names.data[i], file_no++);
      while (o) { Obj *n = o->next; o->next = NULL; *tail = o; tail = &o->next; o = n; }
   }
   if (opt_E) return 0;

   if (!opt_nostdlib)
   {
      DIR *d = opendir(libdir);
      struct dirent *de;
      Vec files = {0};
      if (!d) error("library directory %s not found (use -L)", libdir);
      while ((de = readdir(d)))
      {
         int n = strlen(de->d_name);
         if (n > 2 && !strcmp(de->d_name + n - 2, ".c"))
            vec_push(&files, format("%s/%s", libdir, de->d_name));
      }
      closedir(d);
      qsort(files.data, files.len, sizeof(char *), cmp_str);
      add_asm_units(libdir, &tail);
      for (i = 0; i < files.len; i++)
      {
         Obj *o = compile_file(files.data[i], file_no++);
         while (o) { Obj *n = o->next; o->next = NULL; *tail = o; tail = &o->next; o = n; }
      }
   }

   fp = fopen(asmname, "w");
   if (!fp) error("cannot write %s", asmname);
   {
      /* STORE path for BS9: relative to the current directory, '/' only */
      char *o = xstrdup(outname), *q;
      for (q = o; *q; q++) if (*q == '\\') *q = '/';
      gopts.outname = o;
   }
   codegen(prog, fp, &gopts);
   fclose(fp);
   if (opt_S) return 0;

   {
      char *cmd;
      int rc;
      cmd = format("\"%s\" -q \"%s\"", bs9_path, asmname);
      if (opt_verbose) fprintf(stderr, "%s\n", cmd);
      rc = system(cmd);
      if (rc != 0)
      {
         fprintf(stderr, "cc9: assembler failed (see %s)\n", asmname);
         return 1;
      }
      if (!opt_keep)
      {
         char *base = replace_ext(asmname, "");
         remove(asmname);
         remove(format("%s.ls9", base));
         remove(format("%s.sym", base));
      }
   }
   return 0;
}
