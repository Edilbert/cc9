/*
 * spechtrun - headless test runner around the Specht simulator
 * ------------------------------------------------------------
 * Includes specht.c unchanged (CPU core, ROM traps, BIOS intercepts)
 * and replaces only main() and the SDL event loop:
 *
 *   1. boot the real Sparrow ROM until BASIC waits for a key
 *   2. load a Sparrow file (.x) into RAM, like LOAD does
 *   3. type "SYS $xxxx" + RETURN into the keyboard buffer
 *   4. copy every character the BIOS writes to the console to stdout,
 *      feed stdin into the keyboard buffer when the program reads,
 *      stdin EOF is sent as Ctrl-D ($04)
 *   5. stop when the program returns to BASIC; exit code = D & 255
 *
 * Build:  cc -O2 -I stub -DSDL3 -o spechtrun spechtrun.c
 *         (SPECHT_C must point to specht.c, see Makefile)
 *
 * Usage:  spechtrun [-r rom] [-y sym] [-n maxsteps] [-v] file.x [< input]
 *
 * Exit codes: program status, 124 = step limit, 125 = load error
 */

#define main specht_main
#include SPECHT_C
#undef main

static WORD pc_ring[65536];
static unsigned long *prof;
static int  pc_pos, trace_on;

static void dump_trace(void)
{
   int i;
   if (!trace_on) return;
   fprintf(stderr, "last PCs:");
   {
      int first = -1;
      for (i = 0; i < 65536; i++)
      {
         WORD p = pc_ring[(pc_pos + i) & 65535];
         if (p < 0x2000 && p >= 0x0010) { first = i; break; }
      }
      if (first < 0) first = 65536 - 64;
      for (i = first - 40; i < first + 4; i++)
         if (i >= 0) fprintf(stderr, " %04x", pc_ring[(pc_pos + i) & 65535]);
   }
   fprintf(stderr, "\nS=%04x U=%04x X=%04x Y=%04x D=%04x\n", Reg.S, Reg.U, Reg.X, Reg.Y, Acc.D);
}

static int Key_Pending(void)
{
   return Get_Word(keyrdp) != Get_Word(keywrp);
}

static WORD Load_Sparrow(const char *fn, WORD *entry)
{
   FILE *fp;
   BYTE hdr[8];
   int bank, addr, len, i, c;
   BYTE *mem;

   fp = fopen(fn, "rb");
   if (!fp) { fprintf(stderr, "spechtrun: cannot open %s\n", fn); exit(125); }
   if (fread(hdr, 1, 8, fp) != 8 || hdr[0] != 'S')
   {
      fprintf(stderr, "spechtrun: %s is not a Sparrow file\n", fn);
      exit(125);
   }
   bank = hdr[1];
   addr = (hdr[2] << 8) | hdr[3];
   len  = (hdr[4] << 8) | hdr[5];
   if (bank >= BANKS || addr + len > 0xFE00)
   {
      fprintf(stderr, "spechtrun: bad load range %d:%04x+%04x\n", bank, addr, len);
      exit(125);
   }
   mem = addr < 0x2000 ? RAM[0] : RAM[bank];
   for (i = 0; i < len && (c = fgetc(fp)) != EOF; ++i) mem[addr + i] = c;
   fclose(fp);

   /* entry: the BASIC stub "SYS $xxxx" or the load address */
   *entry = addr;
   for (i = 0; i < len - 6; ++i)
   {
      if (mem[addr + i] == 0xA1)              /* SYS token */
      {
         unsigned v;
         if (sscanf((char *)&mem[addr + i + 1], " $%x", &v) == 1)
         {
            *entry = v;
            break;
         }
      }
   }
   return addr;
}

int main(int argc, char *argv[])
{
   int ic, verbose = 0, started = 0, status = 0, c;
   long long n, maxsteps = 2000000000LL;
   const char *prog = NULL;
   WORD entry, ret = 0, ch_out, ch_in, a;
   char cmd[32], *cp;

   for (ic = 1; ic < argc; ++ic)
   {
           if (!strcmp(argv[ic], "-r") && ic+1 < argc) ROM_fn = argv[++ic];
      else if (!strcmp(argv[ic], "-y") && ic+1 < argc)
           { strncpy(SYM_fn, argv[++ic], SLEN-1); }
      else if (!strcmp(argv[ic], "-n") && ic+1 < argc) maxsteps = atoll(argv[++ic]);
      else if (!strcmp(argv[ic], "-v")) verbose = 1;
      else if (!strcmp(argv[ic], "-t")) { trace_on = 1; atexit(dump_trace); }
      else if (!strcmp(argv[ic], "-p")) prof = calloc(65536, sizeof(unsigned long));
      else prog = argv[ic];
   }
   if (!prog)
   {
      fprintf(stderr, "usage: spechtrun [-r rom] [-y sym] [-n steps] [-v] file.x\n");
      return 125;
   }

   ROM = calloc(MAX64K, sizeof(BYTE));
   for (ic = 0; ic < BANKS; ++ic) RAM[ic] = calloc(MAX64K, sizeof(BYTE));
   lf = fopen(verbose ? "specht.log" : "/dev/null", "w");
   SD_fn = "/dev/null";
   Read_ROM();
   Read_Symbols();
   if (!Check_ROM()) fprintf(stderr, "spechtrun: ROM checksum mismatch\n");
   Build_Intercept();
   *SD_cb_sec = 255;
   Init_SDL();
   reset();
   Init_Run();

   /* 1. boot until the BIOS asks for a key */
   for (n = 0; n < 200000000LL; ++n)
   {
      ch_in = Get_Word(Find_Address("BIOS_CH_IN") );
      if (n > 1000 && PC == ch_in) break;
      Step();
   }
   ch_in  = Get_Word(Find_Address("BIOS_CH_IN"));
   ch_out = Get_Word(Find_Address("BIOS_CH_OUT"));
   if (verbose) fprintf(stderr, "boot: %lld steps, out=%04x in=%04x\n", n, ch_out, ch_in);

   /* 2. load program, 3. type SYS command */
   a = Load_Sparrow(prog, &entry);
   if (verbose) fprintf(stderr, "loaded at %04x, entry %04x\n", a, entry);
   sprintf(cmd, "SYS $%04X\n", entry);
   for (cp = cmd; *cp; ++cp) Buffer(*cp);

   /* 4. run */
   for (n = 0; n < maxsteps; ++n)
   {
      if (!started)
      {
         if (PC == entry) { started = 1; ret = Get_Word(Reg.S); }
      }
      else
      {
         pc_ring[pc_pos++ & 65535] = PC;
         if (prof) prof[PC]++;
         if (PC == ret) { status = Acc.D & 255; break; }
         if (PC == ch_out) { putchar(Acc.A); }
         else if (PC == ch_in && !Key_Pending())
         {
            fflush(stdout);
            c = getchar();
            Buffer(c == EOF ? 4 : c);
         }
      }
      Step();
   }
   fflush(stdout);
   if (n >= maxsteps)
   {
      fprintf(stderr, "spechtrun: step limit reached (PC=%04x)\n", PC);
      return 124;
   }
   if (verbose) fprintf(stderr, "exit %d after %lld steps\n", status, n);
   if (prof)
   {
      FILE *pf = fopen("specht.prof", "w");
      int a;
      for (a = 0; a < 65536; a++) if (prof[a]) fprintf(pf, "%04x %lu\n", a, prof[a]);
      fclose(pf);
   }
   return status;
}
