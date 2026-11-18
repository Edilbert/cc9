/*
 * 6309 code generator, output: BS9 assembler source
 *
 * Value locations after gen_expr():
 *   char/short/int/enum/pointer : D   (char values sign/zero extended to D)
 *   long                        : Q = D:W  (D = high word)
 *   float/double/long double    : 8 byte IEEE double on top of the S stack
 *   struct/union                : address of the object in D
 *
 * Frame: U = frame pointer, 0,U = saved U, 2,U = return address,
 *        4,U = first parameter (6,U if a hidden result pointer is passed)
 */
#include "cc9.h"

enum { V_VOID, V_INT, V_LONG, V_FLT, V_STRUCT };

static FILE  *out;
static int    fn_float;      /* current function uses floating point */
static Obj   *current_fn;
static char  *ret_label;
static int    label_count;

int opt_level = 1;

static void gen_expr(Node *node);
static void gen_void(Node *node);
static void gen_stmt(Node *node);
static void gen_addr(Node *node);
static void gen_branch(Node *node, char *label, int when_true);
static void gen_cast(Type *from, Type *to);
static void gen_byte(Node *node);

char *new_label(void) { return format("L%d", label_count++); }

static void emit(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   fprintf(out, "        ");
   vfprintf(out, fmt, ap);
   fprintf(out, "\n");
   va_end(ap);
}

static void emit_label(const char *l) { fprintf(out, "%s\n", l); }

static int vclass(Type *ty)
{
   switch (ty->kind)
   {
      case TY_VOID: return V_VOID;
      case TY_LONG: return V_LONG;
      case TY_FLOAT: case TY_DOUBLE: case TY_LDOUBLE: return V_FLT;
      case TY_STRUCT: case TY_UNION: return V_STRUCT;
      default: return V_INT;
   }
}

static int is_unsigned_ty(Type *ty) { return ty->is_unsigned || ty->kind == TY_PTR; }

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

static int const_int(Node *node, long *val)
{
   long a, b;
   if (!node) return 0;
   switch (node->kind)
   {
      case ND_NUM:
         if (node->ty && is_flonum(node->ty)) return 0;
         *val = (long)node->val;
         return 1;
      case ND_CAST:
         if (!is_integer(node->ty) && node->ty->kind != TY_PTR) return 0;
         if (!is_integer(node->lhs->ty) && node->lhs->ty->kind != TY_PTR) return 0;
         if (!const_int(node->lhs, &a)) return 0;
         switch (node->ty->size)
         {
            case 1: a = node->ty->is_unsigned ? (unsigned char)a : (signed char)a; break;
            case 2: a = is_unsigned_ty(node->ty) ? (unsigned short)a : (short)a; break;
            case 4: a = (int32_t)a; break;
         }
         *val = a;
         return 1;
      case ND_MUL:
         if (!const_int(node->lhs, &a) || !const_int(node->rhs, &b)) return 0;
         *val = a * b;
         return 1;
      case ND_ADD:
         if (!const_int(node->lhs, &a) || !const_int(node->rhs, &b)) return 0;
         *val = a + b;
         return 1;
      case ND_SUB:
         if (!const_int(node->lhs, &a) || !const_int(node->rhs, &b)) return 0;
         *val = a - b;
         return 1;
      case ND_NEG:
         if (!const_int(node->lhs, &a)) return 0;
         *val = -a;
         return 1;
      default:
         return 0;
   }
}

/* ------------------------------------------------------------------ */
/* addressing                                                          */
/* ------------------------------------------------------------------ */

/* Static address: local (n,U) or global (label+n). Returns operand text. */
static int static_addr(Node *node, long extra, char *buf)
{
   long k;
   switch (node->kind)
   {
      case ND_VAR:
         if (node->var->is_local)
            sprintf(buf, "%ld,U", node->var->offset + extra);
         else if (extra) sprintf(buf, "%s+%ld", node->var->asmname, extra);
         else sprintf(buf, "%s", node->var->asmname);
         return 1;
      case ND_MEMBER:
         if (node->member->is_bitfield) return 0;
         return static_addr(node->lhs, extra + node->member->offset, buf);
      case ND_DEREF:
      {
         Node *p = node->lhs;
         /* *(array + const) */
         if (p->kind == ND_ADD && (p->lhs->ty->kind == TY_ARRAY) && const_int(p->rhs, &k))
            return static_addr(p->lhs, extra + k, buf);
         if (p->kind == ND_SUB && (p->lhs->ty->kind == TY_ARRAY) && const_int(p->rhs, &k))
            return static_addr(p->lhs, extra - k, buf);
         if (p->kind == ND_ADDR) return static_addr(p->lhs, extra, buf);
         if (p->kind == ND_CAST && p->lhs->kind == ND_ADDR && p->ty->kind == TY_PTR)
            return static_addr(p->lhs->lhs, extra, buf);
         if (p->kind == ND_VAR && p->ty->kind == TY_ARRAY) return static_addr(p, extra, buf);
         return 0;
      }
      default:
         return 0;
   }
}

/* simple 16 bit operand for D instructions */
static int simple16(Node *node, char *buf)
{
   long v;
   if (const_int(node, &v) && (node->ty->size == 2 || node->ty->kind == TY_PTR))
   {
      sprintf(buf, "#%ld", (long)(short)v);
      return 1;
   }
   if (node->kind == ND_CAST && node->ty->size == 2 &&
       (node->lhs->ty->size == 2) && vclass(node->lhs->ty) == V_INT)
      return simple16(node->lhs, buf);
   if (vclass(node->ty) == V_INT && node->ty->size == 2 &&
       node->ty->kind != TY_ARRAY && node->ty->kind != TY_FUNC)
   {
      if (node->kind == ND_VAR || node->kind == ND_MEMBER || node->kind == ND_DEREF)
         return static_addr(node, 0, buf);
   }
   return 0;
}

/* simple 32 bit operand: returns hi / lo operand texts */
static int simple32(Node *node, char *hi, char *lo)
{
   long v;
   char buf[128];
   if (const_int(node, &v) && node->ty->size == 4)
   {
      uint32_t u = (uint32_t)v;
      sprintf(hi, "#%d", (short)(u >> 16));
      sprintf(lo, "#%d", (short)(u & 0xffff));
      return 1;
   }
   if (node->ty->kind == TY_LONG &&
       (node->kind == ND_VAR || node->kind == ND_MEMBER || node->kind == ND_DEREF))
   {
      if (!static_addr(node, 0, buf)) return 0;
      strcpy(hi, buf);
      static_addr(node, 2, lo);
      return 1;
   }
   return 0;
}

static Node *strip_ptr_cast(Node *n)
{
   while (n->kind == ND_CAST && n->ty->kind == TY_PTR && n->lhs->ty->kind == TY_PTR) n = n->lhs;
   return n;
}

/* p = p + k  (pointer variable) */
static int match_assign_step(Node *a, char *vop, long *k)
{
   Node *r;
   char op2[128];
   if (a->kind != ND_ASSIGN || a->ty->kind != TY_PTR) return 0;
   if (!simple16(a->lhs, vop) || vop[0] == '#') return 0;
   r = strip_ptr_cast(a->rhs);
   if ((r->kind == ND_ADD || r->kind == ND_SUB) && const_int(r->rhs, k))
   {
      if (!simple16(r->lhs, op2) || strcmp(op2, vop)) return 0;
      if (r->kind == ND_SUB) *k = -*k;
      return 1;
   }
   return 0;
}

/* ++p / p++ on a pointer variable: *off = offset of the old value */
static int match_ptr_update(Node *p, char *vop, long *k, long *off)
{
   long c;
   p = strip_ptr_cast(p);
   if (match_assign_step(p, vop, k)) { *off = 0; return 1; }
   if ((p->kind == ND_ADD || p->kind == ND_SUB) && const_int(p->rhs, &c))
   {
      Node *a = strip_ptr_cast(p->lhs);
      if (p->kind == ND_SUB) c = -c;
      if (match_assign_step(a, vop, k) && c == -*k) { *off = c; return 1; }
   }
   return 0;
}

/* Address that can be set up touching only X (D/W preserved).
   emit = 0: test only.  Returns operand with extra offset. */
static int addr_x(Node *node, long extra, char *buf, int do_emit)
{
   long k;
   char tmp[128];
   if (static_addr(node, extra, buf)) return 1;
   switch (node->kind)
   {
      case ND_MEMBER:
         if (node->member->is_bitfield) return 0;
         return addr_x(node->lhs, extra + node->member->offset, buf, do_emit);
      case ND_DEREF:
      {
         Node *p = node->lhs;
         long off;
         if (p->ty->kind == TY_PTR && match_ptr_update(p, tmp, &k, &off))
         {
            if (do_emit)
            {
               emit("LDX %s", tmp);
               emit("LEAX %ld,X", k);
               emit("STX %s", tmp);
            }
            off += extra;
            if (off) sprintf(buf, "%ld,X", off); else strcpy(buf, ",X");
            return 1;
         }
         if (simple16(p, tmp) && tmp[0] != '#')
         {
            if (do_emit) emit("LDX %s", tmp);
            if (extra) sprintf(buf, "%ld,X", extra); else strcpy(buf, ",X");
            return 1;
         }
         if (p->kind == ND_ADD && p->ty->kind == TY_PTR && simple16(p->lhs, tmp) &&
             tmp[0] != '#' && const_int(p->rhs, &k))
         {
            if (do_emit) emit("LDX %s", tmp);
            k += extra;
            if (k) sprintf(buf, "%ld,X", k); else strcpy(buf, ",X");
            return 1;
         }
         if (p->kind == ND_SUB && p->ty->kind == TY_PTR && simple16(p->lhs, tmp) &&
             tmp[0] != '#' && const_int(p->rhs, &k))
         {
            if (do_emit) emit("LDX %s", tmp);
            k = extra - k;
            if (k) sprintf(buf, "%ld,X", k); else strcpy(buf, ",X");
            return 1;
         }
         if (p->kind == ND_CAST && p->ty->kind == TY_PTR && p->lhs->ty->kind == TY_PTR)
         {
            Node d = *node;
            d.lhs = p->lhs;
            return addr_x(&d, extra, buf, do_emit);
         }
         if (const_int(p, &k))
         {
            sprintf(buf, ">%ld", (long)(unsigned short)(k + extra));
            return 1;
         }
         return 0;
      }
      default:
         return 0;
   }
}

/* Address for a load: may clobber D. Returns operand. */
static void addr_load(Node *node, char *buf)
{
   char tmp[128];
   long k;
   if (addr_x(node, 0, buf, 1)) return;
   if (node->kind == ND_DEREF && node->lhs->kind == ND_ADD && node->lhs->ty->kind == TY_PTR)
   {
      Node *p = node->lhs;
      /* base[index] */
      if (simple16(p->lhs, tmp) && tmp[0] != '#')
      {
         gen_expr(p->rhs);
         emit("LDX %s", tmp);
         strcpy(buf, "D,X");
         return;
      }
      if (p->lhs->ty->kind == TY_ARRAY && static_addr(p->lhs, 0, tmp))
      {
         gen_expr(p->rhs);
         if (p->lhs->kind == ND_VAR && p->lhs->var->is_local) emit("LEAX %s", tmp);
         else emit("LDX #%s", tmp);
         strcpy(buf, "D,X");
         return;
      }
   }
   (void)k;
   gen_addr(node);
   emit("TFR D,X");
   strcpy(buf, ",X");
}

/* add offset to an operand of the form "n,R" / ",R" / "label" */
static void opnd_offset(const char *op, int off, char *buf)
{
   const char *comma = strchr(op, ',');
   if (!strcmp(op, "D,X"))
   {
      /* only used with off == 0 */
      if (off) error("internal: offset on D,X");
      strcpy(buf, op);
      return;
   }
   if (comma)
   {
      long n = 0;
      if (comma != op) n = strtol(op, NULL, 10);
      n += off;
      if (n) sprintf(buf, "%ld%s", n, comma); else strcpy(buf, comma);
      return;
   }
   if (op[0] == '>')
   {
      sprintf(buf, ">%ld", strtol(op + 1, NULL, 10) + off);
      return;
   }
   if (off) sprintf(buf, "%s+%d", op, off); else strcpy(buf, op);
}

/* ------------------------------------------------------------------ */
/* loads / stores                                                      */
/* ------------------------------------------------------------------ */

static void extend_char(Type *ty)
{
   if (ty->is_unsigned) emit("CLRA");
   else emit("SEX");
}

static void push_q(void) { emit("PSHSW"); emit("PSHS D"); }
static void pull_q(void) { emit("PULS D"); emit("PULSW"); }

static void call_helper(const char *name) { emit("JSR __%s", name); }

static const char *hooks[][2] = {
   { "___fp_printf_ptr", "___fp_printf" },
   { "___fp_scanf_ptr",  "___fp_scanf" },
};
#define NHOOKS 2

/* X = address of operand */
static void lea_x(const char *op)
{
   if (!strcmp(op, ",X")) return;
   if (!strcmp(op, "D,X")) { emit("LEAX D,X"); return; }
   if (strchr(op, ',')) { emit("LEAX %s", op); return; }
   if (op[0] == '>') { emit("LDX #%s", op + 1); return; }
   emit("LDX #%s", op);
}

/* load value of type ty from operand */
static void load(Type *ty, const char *op)
{
   char b2[128];
   switch (ty->kind)
   {
      case TY_ARRAY: case TY_FUNC: case TY_STRUCT: case TY_UNION:
         if (!strcmp(op, "D,X")) { emit("LEAX D,X"); emit("TFR X,D"); return; }
         if (strchr(op, ',')) { emit("LEAX %s", op); emit("TFR X,D"); }
         else if (op[0] == '>') emit("LDD #%s", op + 1);
         else emit("LDD #%s", op);
         return;
      case TY_CHAR:
         emit("LDB %s", op);
         extend_char(ty);
         return;
      case TY_LONG:
         emit("LDQ %s", op);
         return;
      case TY_FLOAT:
         emit("LDQ %s", op);
         call_helper("_f2d_s");
         return;
      case TY_DOUBLE: case TY_LDOUBLE:
         lea_x(op);
         emit("JSR ___dpush");
         (void)b2;
         return;
      default:
         emit("LDD %s", op);
         return;
   }
}

/* store current value (D / Q) of type ty to operand, value stays in D/Q */
static void store_int(Type *ty, const char *op)
{
   if (ty->kind == TY_CHAR) emit("STB %s", op);
   else if (ty->kind == TY_LONG) emit("STQ %s", op);
   else emit("STD %s", op);
}

/* ------------------------------------------------------------------ */
/* address of lvalue -> D                                              */
/* ------------------------------------------------------------------ */

static void gen_addr(Node *node)
{
   char buf[128];
   switch (node->kind)
   {
      case ND_VAR:
         if (node->var->is_local)
         {
            emit("LEAX %d,U", node->var->offset);
            emit("TFR X,D");
         }
         else emit("LDD #%s", node->var->asmname);
         return;
      case ND_DEREF:
         gen_expr(node->lhs);
         return;
      case ND_MEMBER:
         if (static_addr(node, 0, buf))
         {
            if (strchr(buf, ',')) { emit("LEAX %s", buf); emit("TFR X,D"); }
            else emit("LDD #%s", buf);
            return;
         }
         gen_addr(node->lhs);
         if (node->member->offset) emit("ADDD #%d", node->member->offset);
         return;
      case ND_COMMA:
         gen_void(node->lhs);
         gen_addr(node->rhs);
         return;
      case ND_FUNCALL: case ND_ASSIGN: case ND_COND:
         if (vclass(node->ty) == V_STRUCT) { gen_expr(node); return; }
         break;
      case ND_CAST:
         if (vclass(node->ty) == V_STRUCT) { gen_addr(node->lhs); return; }
         break;
      default:
         break;
   }
   error_tok(node->tok, "not an lvalue");
}

/* ------------------------------------------------------------------ */
/* conversions                                                         */
/* ------------------------------------------------------------------ */

/* convert int/long in D/Q (type from) to long in Q */
static void to_long(Type *from)
{
   if (vclass(from) == V_LONG) return;
   emit("TFR D,W");
   if (from->size == 2 && is_unsigned_ty(from)) emit("CLRD");
   else emit("SEXW");
}

static void gen_cast(Type *from, Type *to)
{
   int fc = vclass(from), tc = vclass(to);
   if (fc == V_FLT || tc == V_FLT) fn_float = 1;
   if (to->kind == TY_VOID)
   {
      if (fc == V_FLT) emit("LEAS 8,S");
      return;
   }
   if (from->kind == TY_ARRAY || from->kind == TY_FUNC) { from = ty_uint; fc = V_INT; }
   if (fc == V_STRUCT || tc == V_STRUCT) return;

   if (fc == V_FLT)
   {
      if (tc == V_FLT)
      {
         if (to->kind == TY_FLOAT && from->kind != TY_FLOAT)
         {
            /* round to float precision */
            call_helper("_d2f_s");
            call_helper("_f2d_s");
         }
         return;
      }
      if (to->is_unsigned && to->size >= 2) call_helper("_d2ul_s");
      else call_helper("_d2l_s");
      if (tc == V_INT)
      {
         emit("TFR W,D");
         if (to->size == 1) extend_char(to);
      }
      return;
   }
   if (tc == V_FLT)
   {
      int uns = is_unsigned_ty(from) && from->size >= 2;
      if (fc == V_INT && from->size == 2 && is_unsigned_ty(from))
      {
         to_long(from);
         uns = 0;
      }
      else to_long(from);
      call_helper(uns ? "_ul2d_s" : "_l2d_s");
      if (to->kind == TY_FLOAT)
      {
         /* long -> float must round once to float */
         gen_cast(ty_double, ty_float);
      }
      return;
   }
   /* integer <-> integer / pointer */
   if (tc == V_LONG)
   {
      if (fc != V_LONG) to_long(from);
      return;
   }
   if (fc == V_LONG) emit("TFR W,D");
   if (to->size == 1)
   {
      if (from->size == 1 && fc != V_LONG && from->is_unsigned == to->is_unsigned) return;
      extend_char(to);
   }
}

/* ------------------------------------------------------------------ */
/* function calls                                                      */
/* ------------------------------------------------------------------ */

static int param_slot(Type *ty, int proto)
{
   switch (vclass(ty))
   {
      case V_INT: return 2;
      case V_LONG: return 4;
      case V_FLT: return (ty->kind == TY_FLOAT && proto) ? 4 : 8;
      case V_STRUCT: return ty->size;
   }
   return 2;
}

static void gen_funcall(Node *node)
{
   Node *args[256], *arg, *fn = node->lhs;
   Type *params[256], *p;
   int nargs = 0, i, argsize = 0;
   int rc = vclass(node->ty);
   Type *fty = node->func_ty;

   for (arg = node->args; arg; arg = arg->next)
   {
      if (nargs >= 255) error_tok(node->tok, "too many arguments");
      args[nargs++] = arg;
   }
   for (i = 0, p = fty->has_proto ? fty->params : NULL; i < nargs; i++)
   {
      params[i] = p;
      if (p) p = p->next;
   }
   if (rc == V_FLT) emit("LEAS -8,S");
   for (i = nargs - 1; i >= 0; i--)
   {
      Node *a = args[i];
      int c = vclass(a->ty);
      gen_expr(a);
      switch (c)
      {
         case V_INT: emit("PSHS D"); argsize += 2; break;
         case V_LONG: push_q(); argsize += 4; break;
         case V_FLT:
            if (params[i] && params[i]->kind == TY_FLOAT)
            {
               call_helper("_d2f_s");
               push_q();
               argsize += 4;
            }
            else argsize += 8;
            break;
         case V_STRUCT:
            emit("TFR D,Y");
            emit("LEAS -%d,S", a->ty->size);
            emit("TFR S,X");
            emit("LDW #%d", a->ty->size);
            emit("TFM Y+,X+");
            argsize += a->ty->size;
            break;
         default:
            break;
      }
   }
   if (rc == V_FLT)
   {
      emit("LEAX %d,S", argsize);
      emit("PSHS X");
      argsize += 2;
   }
   else if (rc == V_STRUCT)
   {
      emit("LEAX %d,U", node->ret_buffer->offset);
      emit("PSHS X");
      argsize += 2;
   }
   if (fn->kind == ND_VAR && fn->ty->kind == TY_FUNC)
      emit("JSR %s", fn->var->asmname);
   else
   {
      char buf[128];
      if (simple16(fn, buf) && buf[0] != '#') emit("LDX %s", buf);
      else
      {
         gen_expr(fn);
         emit("TFR D,X");
      }
      emit("JSR ,X");
   }
   if (argsize) emit("LEAS %d,S", argsize);
   if (rc == V_STRUCT)
   {
      emit("LEAX %d,U", node->ret_buffer->offset);
      emit("TFR X,D");
   }
   else if (rc == V_INT && node->ty->size == 1)
   {
      /* callee returns extended value already */
   }
}

/* ------------------------------------------------------------------ */
/* bit-fields                                                          */
/* ------------------------------------------------------------------ */

static void shift_d(int n, int left, int arith)
{
   if (n >= 16)
   {
      if (left || !arith) emit("CLRD");
      else { emit("TFR A,B"); emit("SEX"); emit("TFR A,B"); }
      return;
   }
   if (n >= 8)
   {
      if (left) { emit("TFR B,A"); emit("CLRB"); }
      else if (arith) { emit("TFR A,B"); emit("SEX"); }
      else { emit("TFR A,B"); emit("CLRA"); }
      n -= 8;
      if (left) while (n--) emit("LSLA");
      else if (arith) while (n--) emit("ASRB");
      else while (n--) emit("LSRB");
      return;
   }
   while (n-- > 0) emit(left ? "LSLD" : arith ? "ASRD" : "LSRD");
}

static int bf_unsigned(Member *mem)
{
   /* enum bit-fields without negative values are unsigned (like gcc) */
   if (mem->ty->kind == TY_ENUM) return !mem->ty->is_variadic;
   return mem->ty->is_unsigned;
}

static void bitfield_extract(Member *mem)
{
   int w = mem->bit_width;
   unsigned mask = w >= 16 ? 0xffff : (1u << w) - 1;
   shift_d(mem->bit_offset, 0, 0);
   if (w < 16) emit("ANDD #%u", mask);
   if (!bf_unsigned(mem) && w < 16)
   {
      unsigned sb = 1u << (w - 1);
      emit("EORD #%u", sb);
      emit("SUBD #%u", sb);
   }
}

static void gen_bitfield_load(Node *node)
{
   char op[128];
   Node unit = *node;
   (void)unit;
   if (addr_x(node->lhs, node->member->offset, op, 1))
      emit("LDD %s", op);
   else
   {
      gen_addr(node->lhs);
      emit("TFR D,X");
      emit("LDD %d,X", node->member->offset);
   }
   bitfield_extract(node->member);
   if (node->ty->kind == TY_LONG) to_long(node->member->ty->is_unsigned ? ty_uint : ty_int);
}

static void gen_bitfield_store(Node *node)
{
   Node *lhs = node->lhs;
   Member *mem = lhs->member;
   int w = mem->bit_width;
   unsigned wmask = w >= 16 ? 0xffff : (1u << w) - 1;
   unsigned mask = (wmask << mem->bit_offset) & 0xffff;
   char op[128];
   int computed = !addr_x(lhs->lhs, mem->offset, op, 0);
   if (computed)
   {
      gen_addr(lhs->lhs);
      if (mem->offset) emit("ADDD #%d", mem->offset);
      emit("PSHS D");
   }
   gen_expr(node->rhs);
   if (vclass(node->rhs->ty) == V_LONG) emit("TFR W,D");
   emit("PSHS D");               /* value */
   emit("ANDD #%u", wmask);
   shift_d(mem->bit_offset, 1, 0);
   emit("PSHS D");               /* shifted value */
   if (computed) { emit("LDX 4,S"); strcpy(op, ",X"); }
   else addr_x(lhs->lhs, mem->offset, op, 1);
   emit("LDD %s", op);
   emit("ANDD #%u", (~mask) & 0xffff);
   emit("ORD ,S++");
   emit("STD %s", op);
   emit("PULS D");
   if (computed) emit("LEAS 2,S");
   /* value of the assignment = new bit-field value */
   emit("ANDD #%u", wmask);
   if (!bf_unsigned(mem) && w < 16)
   {
      unsigned sb = 1u << (w - 1);
      emit("EORD #%u", sb);
      emit("SUBD #%u", sb);
   }
   if (node->ty->kind == TY_LONG) to_long(ty_int);
}

/* ------------------------------------------------------------------ */
/* assignment                                                          */
/* ------------------------------------------------------------------ */

static void copy_struct(int size)
{
   /* Y = source, X = destination */
   emit("LDW #%d", size);
   emit("TFM Y+,X+");
}

static void gen_assign(Node *node, int need_value)
{
   Node *lhs = node->lhs;
   Type *ty = lhs->ty;
   int c = vclass(ty);
   char op[128], op4[128];

   if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
   {
      gen_bitfield_store(node);
      return;
   }
   if (c == V_STRUCT)
   {
      if (addr_x(lhs, 0, op, 0))
      {
         gen_expr(node->rhs);
         emit("TFR D,Y");
         addr_x(lhs, 0, op, 1);
         if (strchr(op, ',')) { if (strcmp(op, ",X")) emit("LEAX %s", op); }
         else if (op[0] == '>') emit("LDX #%s", op + 1);
         else emit("LDX #%s", op);
      }
      else
      {
         gen_addr(lhs);
         emit("PSHS D");
         gen_expr(node->rhs);
         emit("TFR D,Y");
         emit("PULS X");
      }
      emit("PSHS X");
      copy_struct(ty->size);
      emit("PULS D");
      return;
   }
   if (ty->kind == TY_CHAR && !need_value && addr_x(lhs, 0, op, 0) &&
       node->rhs->ty->kind == TY_CHAR)
   {
      gen_byte(node->rhs);
      addr_x(lhs, 0, op, 1);
      emit("STB %s", op);
      return;
   }
   if (addr_x(lhs, 0, op, 0))
   {
      gen_expr(node->rhs);
      if (c == V_FLT)
      {
         if (ty->kind == TY_FLOAT)
         {
            call_helper("_d2f_s");
            addr_x(lhs, 0, op, 1);
            emit("STQ %s", op);
            if (need_value) load(ty, op);
            return;
         }
         addr_x(lhs, 0, op, 1);
         lea_x(op);
         emit("JSR ___dpop");
         if (need_value) load(ty, ",X");
         (void)op4;
         return;
      }
      addr_x(lhs, 0, op, 1);
      store_int(ty, op);
      return;
   }
   gen_addr(lhs);
   emit("PSHS D");
   gen_expr(node->rhs);
   if (c == V_FLT)
   {
      if (ty->kind == TY_FLOAT)
      {
         call_helper("_d2f_s");
         emit("PULS X");
         emit("STQ ,X");
         if (need_value) load(ty, ",X");
         return;
      }
      emit("LDX 8,S");
      emit("JSR ___dpop");
      emit("LEAS 2,S");
      if (need_value) load(ty, ",X");
      return;
   }
   emit("PULS X");
   store_int(ty, ",X");
}

/* ------------------------------------------------------------------ */
/* arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int is_pow2(long v)
{
   int n = 0;
   if (v <= 0) return -1;
   while (!(v & 1)) { v >>= 1; n++; }
   return v == 1 ? n : -1;
}

/* evaluate binary 16 bit: lhs in D, rhs as operand text */
static int is_commutative(NodeKind k)
{
   return k == ND_ADD || k == ND_MUL || k == ND_BITAND || k == ND_BITOR || k == ND_BITXOR;
}

static void binop16_operands(Node *node, char *op)
{
   if (simple16(node->rhs, op))
   {
      gen_expr(node->lhs);
      return;
   }
   if (is_commutative(node->kind) && simple16(node->lhs, op))
   {
      gen_expr(node->rhs);
      return;
   }
   gen_expr(node->rhs);
   emit("PSHS D");
   gen_expr(node->lhs);
   strcpy(op, ",S++");
}

static void gen_int_binary(Node *node)
{
   char op[128];
   long k;
   int uns = is_unsigned_ty(node->ty);
   switch (node->kind)
   {
      case ND_ADD: case ND_SUB: case ND_BITAND: case ND_BITOR: case ND_BITXOR:
      {
         const char *ins = node->kind == ND_ADD ? "ADDD" : node->kind == ND_SUB ? "SUBD" :
                           node->kind == ND_BITAND ? "ANDD" : node->kind == ND_BITOR ? "ORD" : "EORD";
         if ((node->kind == ND_ADD || node->kind == ND_SUB || node->kind == ND_BITOR ||
              node->kind == ND_BITXOR) && const_int(node->rhs, &k) && (short)k == 0)
         {
            gen_expr(node->lhs);
            return;
         }
         if (node->kind == ND_BITAND && const_int(node->rhs, &k))
         {
            unsigned short m = (unsigned short)k;
            gen_expr(node->lhs);
            if (m == 0xFFFF) return;
            if (m == 0) { emit("CLRD"); return; }
            if (m == 0x00FF) { emit("CLRA"); return; }
            if (m == 0xFF00) { emit("CLRB"); return; }
            if ((m & 0xFF00) == 0xFF00) { emit("ANDB #%u", m & 0xFF); return; }
            if ((m & 0xFF00) == 0) { emit("CLRA"); emit("ANDB #%u", m & 0xFF); return; }
            if ((m & 0xFF) == 0xFF) { emit("ANDA #%u", m >> 8); return; }
            emit("ANDD #%u", m);
            return;
         }
         if (node->kind == ND_BITXOR && const_int(node->rhs, &k) && (unsigned short)k == 0xFFFF)
         {
            gen_expr(node->lhs);
            emit("COMD");
            return;
         }
         binop16_operands(node, op);
         emit("%s %s", ins, op);
         return;
      }
      case ND_MUL:
      {
         int n;
         if (const_int(node->rhs, &k) && (n = is_pow2((unsigned short)k)) >= 0)
         {
            gen_expr(node->lhs);
            shift_d(n, 1, 0);
            return;
         }
         if (const_int(node->lhs, &k) && (n = is_pow2((unsigned short)k)) >= 0)
         {
            gen_expr(node->rhs);
            shift_d(n, 1, 0);
            return;
         }
         binop16_operands(node, op);
         emit("MULD %s", op);
         emit("TFR W,D");
         return;
      }
      case ND_DIV: case ND_MOD:
      {
         int n;
         if (uns && const_int(node->rhs, &k) && (n = is_pow2((unsigned short)k)) >= 0)
         {
            gen_expr(node->lhs);
            if (node->kind == ND_DIV) shift_d(n, 0, 0);
            else emit("ANDD #%ld", (long)((unsigned short)k - 1));
            return;
         }
         if (uns)
         {
            if (simple16(node->rhs, op))
            {
               gen_expr(node->lhs);
               emit("LDX %s", op);
            }
            else
            {
               gen_expr(node->rhs);
               emit("PSHS D");
               gen_expr(node->lhs);
               emit("PULS X");
            }
            call_helper("_udiv16");
            if (node->kind == ND_MOD) emit("TFR X,D");
            return;
         }
         binop16_operands(node, op);
         if (!strcmp(op, ",S++"))
         {
            emit("TFR D,W");
            emit("SEXW");
            emit("DIVQ ,S++");
         }
         else
         {
            emit("TFR D,W");
            emit("SEXW");
            emit("DIVQ %s", op);
         }
         if (node->kind == ND_DIV) emit("TFR W,D");
         return;
      }
      case ND_SHL: case ND_SHR:
      {
         int left = node->kind == ND_SHL;
         int arith = !left && !is_unsigned_ty(node->lhs->ty);
         if (const_int(node->rhs, &k))
         {
            gen_expr(node->lhs);
            shift_d((int)k, left, arith);
            return;
         }
         {
            char *l1 = new_label(), *l2 = new_label();
            gen_expr(node->rhs);
            emit("PSHS D");
            gen_expr(node->lhs);
            emit("LDE 1,S");
            emit("LEAS 2,S");
            emit("BEQ %s", l2);
            emit_label(l1);
            emit(left ? "LSLD" : arith ? "ASRD" : "LSRD");
            emit("DECE");
            emit("BNE %s", l1);
            emit_label(l2);
         }
         return;
      }
      default:
         error_tok(node->tok, "internal: int binary");
   }
}

static void gen_long_binary(Node *node)
{
   char hi[128], lo[128];
   long k;
   switch (node->kind)
   {
      case ND_ADD: case ND_SUB:
      {
         int add = node->kind == ND_ADD;
         if (add && !simple32(node->rhs, hi, lo) && simple32(node->lhs, hi, lo))
         {
            gen_expr(node->rhs);
            emit("ADDW %s", lo);
            emit("ADCD %s", hi);
            return;
         }
         if (simple32(node->rhs, hi, lo))
         {
            gen_expr(node->lhs);
            emit(add ? "ADDW %s" : "SUBW %s", lo);
            emit(add ? "ADCD %s" : "SBCD %s", hi);
            return;
         }
         gen_expr(node->rhs);
         push_q();
         gen_expr(node->lhs);
         emit(add ? "ADDW 2,S" : "SUBW 2,S");
         emit(add ? "ADCD ,S" : "SBCD ,S");
         emit("LEAS 4,S");
         return;
      }
      case ND_BITAND: case ND_BITOR: case ND_BITXOR:
      {
         const char *ins = node->kind == ND_BITAND ? "ANDD" : node->kind == ND_BITOR ? "ORD" : "EORD";
         if (const_int(node->rhs, &k))
         {
            uint32_t u = (uint32_t)k;
            unsigned h = u >> 16, l = u & 0xFFFF;
            gen_expr(node->lhs);
            if (node->kind == ND_BITAND)
            {
               if (h == 0) emit("CLRD"); else if (h != 0xFFFF) emit("ANDD #%u", h);
               if (l == 0) emit("CLRW");
               else if (l != 0xFFFF) { emit("EXG D,W"); emit("ANDD #%u", l); emit("EXG D,W"); }
            }
            else if (node->kind == ND_BITOR)
            {
               if (h == 0xFFFF) emit("LDD #-1"); else if (h) emit("ORD #%u", h);
               if (l == 0xFFFF) emit("LDW #-1");
               else if (l) { emit("EXG D,W"); emit("ORD #%u", l); emit("EXG D,W"); }
            }
            else
            {
               if (h == 0xFFFF) emit("COMD"); else if (h) emit("EORD #%u", h);
               if (l == 0xFFFF) emit("COMW");
               else if (l) { emit("EXG D,W"); emit("EORD #%u", l); emit("EXG D,W"); }
            }
            return;
         }
         if (!simple32(node->rhs, hi, lo) && simple32(node->lhs, hi, lo))
         {
            gen_expr(node->rhs);
            emit("%s %s", ins, hi);
            emit("EXG D,W");
            emit("%s %s", ins, lo);
            emit("EXG D,W");
            return;
         }
         if (simple32(node->rhs, hi, lo))
         {
            gen_expr(node->lhs);
            emit("%s %s", ins, hi);
            emit("EXG D,W");
            emit("%s %s", ins, lo);
            emit("EXG D,W");
            return;
         }
         gen_expr(node->rhs);
         push_q();
         gen_expr(node->lhs);
         emit("%s ,S", ins);
         emit("EXG D,W");
         emit("%s 2,S", ins);
         emit("EXG D,W");
         emit("LEAS 4,S");
         return;
      }
      case ND_MUL: case ND_DIV: case ND_MOD:
      {
         const char *fn;
         int uns = node->ty->is_unsigned;
         if (node->kind == ND_MUL) fn = "_lmul";
         else if (node->kind == ND_DIV) fn = uns ? "_uldiv" : "_ldiv";
         else fn = uns ? "_ulmod" : "_lmod";
         gen_expr(node->rhs);
         push_q();
         gen_expr(node->lhs);
         call_helper(fn);
         emit("LEAS 4,S");
         return;
      }
      case ND_SHL: case ND_SHR:
      {
         int left = node->kind == ND_SHL;
         int arith = !left && !node->lhs->ty->is_unsigned;
         if (const_int(node->rhs, &k))
         {
            int n = (int)k;
            gen_expr(node->lhs);
            if (n >= 32) { emit("CLRD"); emit("CLRW"); if (arith) {} return; }
            if (n >= 16)
            {
               if (left) { emit("TFR W,D"); emit("CLRW"); }
               else if (arith) { emit("TFR D,W"); emit("SEXW"); }
               else { emit("TFR D,W"); emit("CLRD"); }
               n -= 16;
            }
            while (n >= 8)
            {
               if (left) { emit("TFR B,A"); emit("TFR E,B"); emit("TFR F,E"); emit("CLRF"); }
               else
               {
                  emit("TFR E,F"); emit("TFR B,E"); emit("TFR A,B");
                  emit(arith ? "SEX" : "CLRA");
               }
               n -= 8;
            }
            while (n-- > 0)
            {
               if (left) { emit("ADDR W,W"); emit("ADCR D,D"); }
               else if (arith) { emit("ASRD"); emit("RORW"); }
               else { emit("LSRD"); emit("RORW"); }
            }
            return;
         }
         gen_expr(node->rhs);
         emit("PSHS D");
         gen_expr(node->lhs);
         call_helper(left ? "_lshl" : arith ? "_lsar" : "_lshr");
         emit("LEAS 2,S");
         return;
      }
      default:
         error_tok(node->tok, "internal: long binary");
   }
}

static void gen_flt_binary(Node *node)
{
   const char *fn = node->kind == ND_ADD ? "_dadd" : node->kind == ND_SUB ? "_dsub" :
                    node->kind == ND_MUL ? "_dmul" : node->kind == ND_DIV ? "_ddiv" : NULL;
   if (!fn) error_tok(node->tok, "invalid operands to floating point operation");
   gen_expr(node->lhs);
   gen_expr(node->rhs);
   call_helper(format("%s_s", fn));
   if (node->ty->kind == TY_FLOAT) gen_cast(ty_double, ty_float);
}

/* ------------------------------------------------------------------ */
/* comparisons and conditions                                          */
/* ------------------------------------------------------------------ */

#define ND_GT_ 1000
#define ND_GE_ 1001
static const char *cond_branch(int kind, int uns, int when_true)
{
   switch (kind)
   {
      case ND_GT_: return when_true ? (uns ? "LBHI" : "LBGT") : (uns ? "LBLS" : "LBLE");
      case ND_GE_: return when_true ? (uns ? "LBHS" : "LBGE") : (uns ? "LBLO" : "LBLT");
      case ND_EQ: return when_true ? "LBEQ" : "LBNE";
      case ND_NE: return when_true ? "LBNE" : "LBEQ";
      case ND_LT: return when_true ? (uns ? "LBLO" : "LBLT") : (uns ? "LBHS" : "LBGE");
      case ND_LE: return when_true ? (uns ? "LBLS" : "LBLE") : (uns ? "LBHI" : "LBGT");
      default: return NULL;
   }
}

/* load a char value into B (A undefined) */
static void gen_byte(Node *node)
{
   char op[128];
   if (node->kind == ND_CAST && node->ty->kind == TY_CHAR)
   {
      Type *f = node->lhs->ty;
      if (f->kind == TY_CHAR) { gen_byte(node->lhs); return; }
      if (vclass(f) == V_INT) { gen_expr(node->lhs); return; }
      if (vclass(f) == V_LONG) { gen_expr(node->lhs); emit("TFR W,D"); return; }
   }
   if (node->ty->kind == TY_CHAR &&
       (node->kind == ND_VAR || node->kind == ND_DEREF ||
        (node->kind == ND_MEMBER && !node->member->is_bitfield)))
   {
      addr_load(node, op);
      emit("LDB %s", op);
      return;
   }
   gen_expr(node);
}

static Node *char_operand(Node *n)
{
   if (n->kind == ND_CAST && n->lhs->ty->kind == TY_CHAR && n->ty->size == 2) return n->lhs;
   return NULL;
}

/* emit compare, return 1 if unsigned semantic applies, flags represent lhs ? rhs
   (for long and float: D holds -1/0/1 (2 = unordered) compared against 0) */
static int gen_compare(Node *node, int *special, int *kind)
{
   Node *lhs = node->lhs, *rhs = node->rhs;
   int c = vclass(lhs->ty);
   char op[128];
   *special = 0;
   *kind = node->kind;
   if (c == V_INT)
   {
      Node *cl = char_operand(lhs);
      long k;
      if (cl && const_int(rhs, &k) &&
          ((cl->ty->is_unsigned && k >= 0 && k <= 255) ||
           (!cl->ty->is_unsigned && k >= -128 && k <= 127)))
      {
         gen_byte(cl);
         if (k == 0 && (node->kind == ND_EQ || node->kind == ND_NE)) emit("TSTB");
         else emit("CMPB #%ld", k);
         return cl->ty->is_unsigned;
      }
      cl = char_operand(rhs);
      if (cl && const_int(lhs, &k) &&
          ((cl->ty->is_unsigned && k >= 0 && k <= 255) ||
           (!cl->ty->is_unsigned && k >= -128 && k <= 127)))
      {
         /* k op c  <=>  c op' k */
         gen_byte(cl);
         if (k == 0 && (node->kind == ND_EQ || node->kind == ND_NE)) emit("TSTB");
         else emit("CMPB #%ld", k);
         if (node->kind == ND_LT) *kind = ND_GT_;
         else if (node->kind == ND_LE) *kind = ND_GE_;
         return cl->ty->is_unsigned;
      }
      if (!simple16(rhs, op) && simple16(lhs, op))
      {
         /* swap operands: a < b  <=>  b > a */
         gen_expr(rhs);
         emit("CMPD %s", op);
         if (node->kind == ND_LT) *kind = ND_GT_;
         else if (node->kind == ND_LE) *kind = ND_GE_;
         return is_unsigned_ty(lhs->ty);
      }
      if (simple16(rhs, op))
      {
         gen_expr(lhs);
         if (!strcmp(op, "#0") && (node->kind == ND_EQ || node->kind == ND_NE ||
                                   !is_unsigned_ty(lhs->ty)))
            emit("TSTD");
         else emit("CMPD %s", op);
      }
      else
      {
         gen_expr(rhs);
         emit("PSHS D");
         gen_expr(lhs);
         emit("CMPD ,S++");
      }
      return is_unsigned_ty(lhs->ty);
   }
   if (c == V_LONG)
   {
      gen_expr(rhs);
      push_q();
      gen_expr(lhs);
      call_helper(lhs->ty->is_unsigned ? "_ulcmp" : "_lcmp");
      emit("LEAS 4,S");
      emit("CMPD #0");
      return 0;
   }
   if (c == V_FLT)
   {
      gen_expr(lhs);
      gen_expr(rhs);
      call_helper("_dcmp_s");
      *special = 1;
      return 0;
   }
   error_tok(node->tok, "invalid comparison");
   return 0;
}

/* float compare result in D: -1 less, 0 equal, 1 greater, 2 unordered */
static void flt_branch(NodeKind kind, char *label, int when_true)
{
   char *skip = new_label();
   switch (kind)
   {
      case ND_EQ:
         emit("CMPD #0");
         emit(when_true ? "LBEQ %s" : "LBNE %s", label);
         break;
      case ND_NE:
         emit("CMPD #0");
         emit(when_true ? "LBNE %s" : "LBEQ %s", label);
         break;
      case ND_LT:
         emit("CMPD #-1");
         emit(when_true ? "LBEQ %s" : "LBNE %s", label);
         break;
      case ND_LE:
         /* true for -1 and 0 */
         emit("ADDD #1");
         emit("CMPD #1");
         emit(when_true ? "LBLS %s" : "LBHI %s", label);
         break;
      default:
         break;
   }
   emit_label(skip);
}

static void test_value(Type *ty)
{
   switch (vclass(ty))
   {
      case V_LONG: emit("ORR W,D"); break;
      case V_FLT:
         emit("LDD ,S");
         emit("ANDA #$7F");
         emit("ORD 2,S");
         emit("ORD 4,S");
         emit("ORD 6,S");
         emit("LEAS 8,S");
         break;
      default:
         emit("TSTD");
         break;
   }
}

static int gen_long_branch(Node *node, char *label, int when_true)
{
   char hi[128], lo[128];
   Node *lhs = node->lhs, *rhs = node->rhs;
   int uns = is_unsigned_ty(lhs->ty);
   long v;
   if (vclass(lhs->ty) != V_LONG || !simple32(rhs, hi, lo)) return 0;
   gen_expr(lhs);
   if (node->kind == ND_EQ || node->kind == ND_NE)
   {
      int eq = (node->kind == ND_EQ) == when_true;   /* branch if equal */
      if (const_int(rhs, &v) && v == 0)
      {
         emit("ORR W,D");
         emit(eq ? "LBEQ %s" : "LBNE %s", label);
         return 1;
      }
      if (eq)
      {
         char *skip = new_label();
         emit("CMPD %s", hi);
         emit("BNE %s", skip);
         emit("CMPW %s", lo);
         emit("LBEQ %s", label);
         emit_label(skip);
      }
      else
      {
         emit("CMPD %s", hi);
         emit("LBNE %s", label);
         emit("CMPW %s", lo);
         emit("LBNE %s", label);
      }
      return 1;
   }
   {
      char *skip = new_label();
      const char *hlt = uns ? "LBLO" : "LBLT", *hgt = uns ? "LBHI" : "LBGT";
      int lt = node->kind == ND_LT;
      emit("CMPD %s", hi);
      if (when_true)
      {
         emit("%s %s", hlt, label);
         emit("%s %s", hgt, skip);
         emit("CMPW %s", lo);
         emit("%s %s", lt ? "LBLO" : "LBLS", label);
      }
      else
      {
         emit("%s %s", hlt, skip);
         emit("%s %s", hgt, label);
         emit("CMPW %s", lo);
         emit("%s %s", lt ? "LBHS" : "LBHI", label);
      }
      emit_label(skip);
   }
   return 1;
}

static void gen_branch(Node *node, char *label, int when_true)
{
   long v;
   switch (node->kind)
   {
      case ND_LOGAND:
         if (!when_true)
         {
            gen_branch(node->lhs, label, 0);
            gen_branch(node->rhs, label, 0);
         }
         else
         {
            char *skip = new_label();
            gen_branch(node->lhs, skip, 0);
            gen_branch(node->rhs, label, 1);
            emit_label(skip);
         }
         return;
      case ND_LOGOR:
         if (when_true)
         {
            gen_branch(node->lhs, label, 1);
            gen_branch(node->rhs, label, 1);
         }
         else
         {
            char *skip = new_label();
            gen_branch(node->lhs, skip, 1);
            gen_branch(node->rhs, label, 0);
            emit_label(skip);
         }
         return;
      case ND_NOT:
         gen_branch(node->lhs, label, !when_true);
         return;
      case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
      {
         int special, uns, kind;
         if (gen_long_branch(node, label, when_true)) return;
         uns = gen_compare(node, &special, &kind);
         if (special) flt_branch(node->kind, label, when_true);
         else emit("%s %s", cond_branch(kind, uns, when_true), label);
         return;
      }
      case ND_COMMA:
         gen_void(node->lhs);
         gen_branch(node->rhs, label, when_true);
         return;
      default:
         break;
   }
   if (const_int(node, &v))
   {
      if ((v != 0) == when_true) emit("LBRA %s", label);
      return;
   }
   if (node->kind == ND_NUM && is_flonum(node->ty))
   {
      if ((node->fval != 0) == when_true) emit("LBRA %s", label);
      return;
   }
   if (node->ty->kind == TY_CHAR || char_operand(node))
   {
      gen_byte(node->ty->kind == TY_CHAR ? node : char_operand(node));
      emit("TSTB");
      emit(when_true ? "LBNE %s" : "LBEQ %s", label);
      return;
   }
   gen_expr(node);
   test_value(node->ty);
   emit(when_true ? "LBNE %s" : "LBEQ %s", label);
}

static void gen_bool_value(Node *node)
{
   char *lf = new_label(), *le = new_label();
   gen_branch(node, lf, 0);
   emit("LDD #1");
   emit("BRA %s", le);
   emit_label(lf);
   emit("CLRD");
   emit_label(le);
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static HashMap dconst_map;
static Vec     dconst_list;

static void push_double_const(double d)
{
   uint64_t u;
   char key[32], *label;
   memcpy(&u, &d, 8);
   if (u == 0)
   {
      emit("CLRD");
      emit("CLRW");
      push_q();
      push_q();
      return;
   }
   sprintf(key, "%016llx", (unsigned long long)u);
   label = hm_get(&dconst_map, key);
   if (!label)
   {
      label = format("DC$%s", key);
      hm_put(&dconst_map, xstrdup(key), label);
      vec_push(&dconst_list, label);
   }
   emit("LDX #%s", label);
   emit("JSR ___dpush");
}

static void load_q_const(uint32_t u)
{
   if (u == 0) { emit("CLRD"); emit("CLRW"); return; }
   if (u == 0x5A5AA5A5u) { emit("LDD #$5A5A"); emit("LDW #$A5A5"); return; }
   emit("LDQ #%d", (int32_t)u);
}

static void gen_expr(Node *node)
{
   char op[128];
   long v;
   int c = vclass(node->ty);

   if (c == V_FLT) fn_float = 1;
   if ((c == V_INT || c == V_LONG) && node->kind != ND_NUM && node->kind != ND_VAR &&
       const_int(node, &v))
   {
      if (c == V_LONG) load_q_const((uint32_t)v);
      else if ((short)v == 0) emit("CLRD");
      else emit("LDD #%d", (short)v);
      return;
   }
   switch (node->kind)
   {
      case ND_NULL_EXPR:
         return;
      case ND_NUM:
         if (c == V_FLT)
         {
            double d = node->fval;
            if (node->ty->kind == TY_FLOAT) d = (float)d;
            push_double_const(d);
            return;
         }
         if (c == V_LONG) { load_q_const((uint32_t)node->val); return; }
         if ((short)node->val == 0) emit("CLRD");
         else emit("LDD #%d", (short)node->val);
         return;
      case ND_VAR: case ND_MEMBER: case ND_DEREF:
         if (node->kind == ND_MEMBER && node->member->is_bitfield)
         {
            gen_bitfield_load(node);
            return;
         }
         if (node->kind == ND_DEREF && node->ty->kind == TY_FUNC)
         {
            gen_expr(node->lhs);
            return;
         }
         if (node->kind == ND_MEMBER && !static_addr(node, 0, op) &&
             !addr_x(node, 0, op, 0) &&
             (node->lhs->kind == ND_FUNCALL || node->lhs->kind == ND_COND ||
              node->lhs->kind == ND_ASSIGN || node->lhs->kind == ND_COMMA))
         {
            gen_addr(node->lhs);
            emit("TFR D,X");
            sprintf(op, "%d,X", node->member->offset);
            load(node->ty, op);
            return;
         }
         if (node->kind == ND_VAR && node->var->is_local &&
             (node->ty->kind == TY_ARRAY || c == V_STRUCT))
         {
            emit("LEAX %d,U", node->var->offset);
            emit("TFR X,D");
            return;
         }
         addr_load(node, op);
         load(node->ty, op);
         return;
      case ND_ADDR:
         if (node->lhs->kind == ND_VAR && !node->lhs->var->is_local)
         {
            emit("LDD #%s", node->lhs->var->asmname);
            return;
         }
         if (static_addr(node->lhs, 0, op))
         {
            if (strchr(op, ',')) { emit("LEAX %s", op); emit("TFR X,D"); }
            else emit("LDD #%s", op);
            return;
         }
         gen_addr(node->lhs);
         return;
      case ND_ASSIGN:
         gen_assign(node, 1);
         return;
      case ND_COMMA:
         gen_void(node->lhs);
         gen_expr(node->rhs);
         return;
      case ND_COND:
      {
         char *le = new_label(), *lend = new_label();
         gen_branch(node->cond, le, 0);
         gen_expr(node->then);
         emit("LBRA %s", lend);
         emit_label(le);
         gen_expr(node->els);
         emit_label(lend);
         return;
      }
      case ND_CAST:
         gen_expr(node->lhs);
         gen_cast(node->lhs->ty, node->ty);
         return;
      case ND_MEMZERO:
      {
         int size = node->var->ty->size;
         if (size <= 8)
         {
            int i;
            for (i = 0; i + 1 < size; i += 2) emit("CLR %d,U", node->var->offset + i), emit("CLR %d,U", node->var->offset + i + 1);
            if (size & 1) emit("CLR %d,U", node->var->offset + size - 1);
         }
         else
         {
            char *l = new_label();
            emit("LEAX %d,U", node->var->offset);
            emit("LDW #%d", size);
            emit_label(l);
            emit("CLR ,X+");
            emit("DECW");
            emit("BNE %s", l);
         }
         return;
      }
      case ND_FUNCALL:
         gen_funcall(node);
         return;
      case ND_NOT: case ND_LOGAND: case ND_LOGOR:
      case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
         gen_bool_value(node);
         return;
      case ND_NEG:
         gen_expr(node->lhs);
         if (c == V_FLT) { emit("LDA ,S"); emit("EORA #$80"); emit("STA ,S"); }
         else if (c == V_LONG)
         {
            emit("COMD"); emit("COMW"); emit("ADDW #1"); emit("ADCD #0");
         }
         else emit("NEGD");
         return;
      case ND_BITNOT:
         gen_expr(node->lhs);
         if (c == V_LONG) { emit("COMD"); emit("COMW"); }
         else emit("COMD");
         return;
      case ND_ASM:
         return;
      default:
         break;
   }

   /* binary */
   if (const_int(node, &v) && (c == V_INT || c == V_LONG))
   {
      if (c == V_LONG) load_q_const((uint32_t)v);
      else if ((short)v == 0) emit("CLRD");
      else emit("LDD #%d", (short)v);
      return;
   }
   switch (c)
   {
      case V_INT: gen_int_binary(node); return;
      case V_LONG: gen_long_binary(node); return;
      case V_FLT: gen_flt_binary(node); return;
      default: break;
   }
   error_tok(node->tok, "invalid expression");
}

/* evaluate and discard */
static void gen_void(Node *node)
{
   if (!node) return;
   switch (node->kind)
   {
      case ND_ASSIGN:
         gen_assign(node, 0);
         if (vclass(node->ty) == V_FLT) {}
         return;
      case ND_COMMA:
         gen_void(node->lhs);
         gen_void(node->rhs);
         return;
      case ND_NULL_EXPR: case ND_NUM: case ND_VAR:
         return;
      case ND_CAST:
         if (node->ty->kind == TY_VOID) { gen_void(node->lhs); return; }
         /* x++ as statement: (T)((x += 1) - 1) */
         if (node->lhs->kind == ND_ADD && node->lhs->rhs &&
             (node->lhs->rhs->kind == ND_NUM || node->lhs->rhs->kind == ND_CAST ||
              node->lhs->rhs->kind == ND_MUL) &&
             (node->lhs->lhs->kind == ND_ASSIGN || node->lhs->lhs->kind == ND_COMMA))
         {
            long k;
            if (const_int(node->lhs->rhs, &k)) { gen_void(node->lhs->lhs); return; }
         }
         break;
      case ND_COND:
      {
         char *le = new_label(), *lend = new_label();
         gen_branch(node->cond, le, 0);
         gen_void(node->then);
         emit("LBRA %s", lend);
         emit_label(le);
         gen_void(node->els);
         emit_label(lend);
         return;
      }
      default:
         break;
   }
   gen_expr(node);
   if (vclass(node->ty) == V_FLT) emit("LEAS 8,S");
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void emit_asm_text(const char *s)
{
   const char *p = s;
   while (*p)
   {
      const char *e = strchr(p, '\n');
      int len = e ? (int)(e - p) : (int)strlen(p);
      if (len > 0) fprintf(out, "%.*s\n", len, p);
      if (!e) break;
      p = e + 1;
   }
}

static void gen_stmt(Node *node)
{
   Node *n;
   switch (node->kind)
   {
      case ND_IF:
      {
         char *le = new_label(), *lend = new_label();
         gen_branch(node->cond, le, 0);
         gen_stmt(node->then);
         if (node->els)
         {
            emit("LBRA %s", lend);
            emit_label(le);
            gen_stmt(node->els);
            emit_label(lend);
         }
         else emit_label(le);
         return;
      }
      case ND_FOR:
      {
         char *lbegin = new_label();
         if (node->init) gen_stmt(node->init);
         emit_label(lbegin);
         if (node->cond) gen_branch(node->cond, node->brk_label, 0);
         gen_stmt(node->then);
         emit_label(node->cont_label);
         if (node->inc) gen_void(node->inc);
         emit("LBRA %s", lbegin);
         emit_label(node->brk_label);
         return;
      }
      case ND_DO:
      {
         char *lbegin = new_label();
         emit_label(lbegin);
         gen_stmt(node->then);
         emit_label(node->cont_label);
         gen_branch(node->cond, lbegin, 1);
         emit_label(node->brk_label);
         return;
      }
      case ND_SWITCH:
      {
         int lng = vclass(node->cond->ty) == V_LONG;
         gen_expr(node->cond);
         for (n = node->case_next; n; n = n->case_next)
         {
            if (lng)
            {
               char *skip = new_label();
               uint32_t u = (uint32_t)n->case_val;
               emit("CMPD #%d", (short)(u >> 16));
               emit("BNE %s", skip);
               emit("CMPW #%d", (short)(u & 0xffff));
               emit("LBEQ %s", n->label);
               emit_label(skip);
            }
            else
            {
               emit("CMPD #%d", (short)n->case_val);
               emit("LBEQ %s", n->label);
            }
         }
         if (node->default_case) emit("LBRA %s", node->default_case->label);
         else emit("LBRA %s", node->brk_label);
         gen_stmt(node->then);
         emit_label(node->brk_label);
         return;
      }
      case ND_CASE:
         emit_label(node->label);
         gen_stmt(node->lhs);
         return;
      case ND_BLOCK:
         for (n = node->body; n; n = n->next) gen_stmt(n);
         return;
      case ND_GOTO:
         emit("LBRA %s", node->unique_label);
         return;
      case ND_LABEL:
         emit_label(node->unique_label);
         gen_stmt(node->lhs);
         return;
      case ND_RETURN:
         if (node->lhs)
         {
            int c = vclass(node->lhs->ty);
            Type *rt = current_fn->ty->return_ty;
            if (rt->kind == TY_VOID)
            {
               gen_void(node->lhs);
            }
            else
            {
               gen_expr(node->lhs);
               if (c == V_FLT)
               {
                  emit("LDX 4,U");
                  emit("JSR ___dpop");
               }
               else if (c == V_STRUCT)
               {
                  emit("TFR D,Y");
                  emit("LDX 4,U");
                  copy_struct(node->lhs->ty->size);
               }
            }
         }
         emit("LBRA %s", ret_label);
         return;
      case ND_EXPR_STMT:
         gen_void(node->lhs);
         return;
      case ND_ASM:
         emit_asm_text(node->asm_str);
         return;
      default:
         gen_void(node);
         return;
   }
}

/* ------------------------------------------------------------------ */
/* functions                                                           */
/* ------------------------------------------------------------------ */

static void assign_offsets(Obj *fn)
{
   Obj *var;
   Type *p;
   int off = 4, loc = 0, nparams = 0, i;
   int proto = fn->ty->has_proto;
   int rc = vclass(fn->ty->return_ty);
   Obj *params[256];

   fn->hidden_ret = (rc == V_FLT || rc == V_STRUCT);
   if (fn->hidden_ret) off = 6;
   for (p = fn->ty->params; p; p = p->next) nparams++;
   /* fn->params: first parameter, followed by the others */
   for (var = fn->params, i = 0; var && i < nparams; var = var->next)
      if (var->is_param) params[i++] = var;
   for (i = 0; i < nparams; i++)
   {
      Obj *v = params[i];
      int slot = param_slot(v->ty, proto);
      v->offset = off + ((vclass(v->ty) == V_INT && v->ty->size == 1) ? 1 : 0);
      off += slot;
   }
   for (var = fn->locals; var; var = var->next)
   {
      if (var->is_param) continue;
      loc += var->ty->size > 0 ? var->ty->size : 0;
      var->offset = -loc;
   }
   fn->stack_size = loc;
}

static void gen_function(Obj *fn)
{
   int frameless;
   Obj *var;
   Type *p;
   int i, nparams = 0;
   assign_offsets(fn);
   current_fn = fn;
   ret_label = new_label();

   fprintf(out, "\n* %s\n", fn->name);
   emit_label(fn->asmname);
   frameless = fn->stack_size == 0 && fn->ty->params == NULL && !fn->hidden_ret &&
               fn->asms.len == 0;
   if (!frameless)
   {
      emit("PSHS U");
      emit("TFR S,U");
   }
   if (fn->stack_size) emit("LEAS -%d,S", fn->stack_size);

   /* old style definitions: float parameters arrive as double */
   if (!fn->ty->has_proto)
   {
      for (p = fn->ty->params; p; p = p->next) nparams++;
      for (var = fn->params, i = 0; var && i < nparams; var = var->next)
      {
         if (!var->is_param) continue;
         i++;
         if (var->ty->kind == TY_FLOAT)
         {
            emit("LDQ %d,U", var->offset + 4);
            push_q();
            emit("LDQ %d,U", var->offset);
            push_q();
            call_helper("_d2f_s");
            emit("STQ %d,U", var->offset);
         }
      }
   }

   gen_stmt(fn->body);
   if (!strcmp(fn->name, "main") && vclass(fn->ty->return_ty) == V_INT)
      emit("CLRD");           /* main without return: exit status 0 */
   emit_label(ret_label);
   if (frameless) emit("RTS");
   else
   {
      emit("TFR U,S");
      emit("PULS U,PC");
   }
}


/* ------------------------------------------------------------------ */
/* peephole optimizer                                                  */
/* ------------------------------------------------------------------ */

static int is_label_line(const char *l) { return l[0] && l[0] != ' ' && l[0] != '*'; }

static const char *ins_of(const char *l)
{
   if (strncmp(l, "        ", 8)) return NULL;
   return l + 8;
}

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

static int volatile_operand(const char *op)
{
   return strchr(op, '+') && strchr(op, ',') ? 1 : strstr(op, ",-") != NULL || strstr(op, ",S") != NULL;
}

static char *peephole(char *text)
{
   Vec lines = {0};
   char *p = text, *res;
   int i, changed = 1;
   size_t total = 0;
   while (*p)
   {
      char *e = strchr(p, '\n');
      int len = e ? (int)(e - p) : (int)strlen(p);
      vec_push(&lines, xstrndup(p, len));
      p += len;
      if (*p) p++;
   }
   while (changed)
   {
      changed = 0;
      for (i = 0; i < lines.len; i++)
      {
         char *l = lines.data[i], *n = i + 1 < lines.len ? lines.data[i + 1] : NULL;
         const char *a = ins_of(l), *b = n ? ins_of(n) : NULL;
         if (!a) continue;
         /* branch to the next line */
         if ((starts(a, "LBRA ") || starts(a, "BRA ")) && n && is_label_line(n) &&
             !strcmp(strchr(a, ' ') + 1, n))
         {
            lines.data[i] = "";
            changed = 1;
            continue;
         }
         /* dead code after an unconditional jump */
         if (starts(a, "LBRA ") || starts(a, "BRA ") || !strcmp(a, "RTS") ||
             !strcmp(a, "PULS U,PC"))
         {
            int j = i + 1;
            while (j < lines.len && ins_of(lines.data[j]) && *(char *)lines.data[j])
            {
               lines.data[j] = "";
               changed = 1;
               j++;
            }
            continue;
         }
         if (!b) continue;
         /* store followed by load of the same location */
         if ((starts(a, "STD ") && starts(b, "LDD ")) || (starts(a, "STQ ") && starts(b, "LDQ ")) ||
             (starts(a, "STW ") && starts(b, "LDW ")))
         {
            if (!strcmp(a + 4, b + 4) && !volatile_operand(a + 4))
            {
               /* keep flags semantics: only if next is not a conditional branch */
               const char *c = i + 2 < lines.len ? ins_of(lines.data[i + 2]) : NULL;
               if (!(c && c[0] == 'L' && c[1] == 'B') && !(c && c[0] == 'B' && strcmp(c, "BRA")))
               {
                  lines.data[i + 1] = "";
                  changed = 1;
                  continue;
               }
            }
         }
         if (starts(a, "LDB ") && !strcmp(b, "TSTB"))
         {
            lines.data[i + 1] = "";
            changed = 1;
            continue;
         }
         if (!strcmp(a, "PSHS D") && !strcmp(b, "PULS D"))
         {
            lines.data[i] = lines.data[i + 1] = "";
            changed = 1;
            continue;
         }
         if (!strcmp(a, "PSHS D") && !strcmp(b, "PULS X"))
         {
            lines.data[i] = "        TFR D,X";
            lines.data[i + 1] = "";
            changed = 1;
            continue;
         }
         if (!strcmp(a, "TFR D,X") && starts(b, "LDX "))
         {
            lines.data[i] = "";
            changed = 1;
            continue;
         }
         if (!strcmp(a, "TFR X,D") && !strcmp(b, "TFR D,X"))
         {
            lines.data[i + 1] = "";
            changed = 1;
            continue;
         }
         if (starts(a, "ADDD #") && !strcmp(b, "TSTD") && i + 2 < lines.len)
         {
            const char *c = ins_of(lines.data[i + 2]);
            if (c && (starts(c, "LBEQ ") || starts(c, "LBNE ")))
            {
               long k = strtol(a + 6, NULL, 10);
               lines.data[i] = format("        CMPD #%ld", (long)(short)(-k));
               lines.data[i + 1] = "";
               changed = 1;
               continue;
            }
         }
         if ((!strcmp(b, "CMPD #0") || !strcmp(b, "TSTD")) && i + 2 < lines.len)
         {
            const char *c = ins_of(lines.data[i + 2]);
            int eqne = c && (starts(c, "LBEQ ") || starts(c, "LBNE "));
            int sgn = c && (eqne || starts(c, "LBLT ") || starts(c, "LBGE ") ||
                            starts(c, "LBGT ") || starts(c, "LBLE "));
            if ((starts(a, "LDD ") && sgn) || (!strcmp(a, "SEX") && eqne))
            {
               lines.data[i + 1] = "";
               changed = 1;
               continue;
            }
            if (!strcmp(a, "CLRA") && eqne)
            {
               lines.data[i + 1] = "        TSTB";
               changed = 1;
               continue;
            }
         }
      }
      /* jump threading: label followed by LBRA */
      {
         HashMap alias;
         int any = 0;
         memset(&alias, 0, sizeof(alias));
         for (i = 0; i < lines.len; i++)
         {
            char *l = lines.data[i];
            int j;
            if (!is_label_line(l)) continue;
            j = i + 1;
            while (j < lines.len && is_label_line(lines.data[j])) j++;
            if (j < lines.len && ins_of(lines.data[j]) && starts(ins_of(lines.data[j]), "LBRA "))
            {
               char *target = (char *)ins_of(lines.data[j]) + 5;
               if (strcmp(target, l)) { hm_put(&alias, l, target); any = 1; }
            }
         }
         if (any)
            for (i = 0; i < lines.len; i++)
            {
               const char *a = ins_of(lines.data[i]);
               char *sp, *t;
               int depth = 0;
               if (!a || (a[0] != 'L' && a[0] != 'B') || !(sp = strchr(a, ' '))) continue;
               if (!(a[0] == 'B' || (a[0] == 'L' && a[1] == 'B'))) continue;
               t = sp + 1;
               while (hm_get(&alias, t) && depth++ < 10) t = hm_get(&alias, t);
               if (t != sp + 1)
               {
                  lines.data[i] = format("        %.*s %s", (int)(sp - a), a, t);
                  changed = 1;
               }
            }
      }
      /* redundant loads of a constant already in D */
      {
         const char *dval = NULL;
         for (i = 0; i < lines.len; i++)
         {
            const char *a = ins_of(lines.data[i]);
            if (!a) { if (*(char *)lines.data[i]) dval = NULL; continue; }
            if (!strcmp(a, "CLRD") || (starts(a, "LDD #") && !strchr(a, ',')))
            {
               const char *v = !strcmp(a, "CLRD") ? "#0" : a + 4;
               const char *nx = i + 1 < lines.len ? ins_of(lines.data[i + 1]) : NULL;
               int br = nx && (nx[0] == 'B' || (nx[0] == 'L' && nx[1] == 'B'));
               if (dval && !strcmp(dval, v) && !br)
               {
                  lines.data[i] = "";
                  changed = 1;
                  continue;
               }
               dval = v;
               continue;
            }
            if ((starts(a, "STD ") || starts(a, "STB ") || starts(a, "STA ") ||
                 starts(a, "STX ") || starts(a, "LDX ") || starts(a, "LEAX ") ||
                 starts(a, "LEAY ") || starts(a, "LEAS ") || starts(a, "STY ") ||
                 starts(a, "LDY ") || starts(a, "CLR ") || !strcmp(a, "PSHS D") ||
                 !strcmp(a, "PSHS X") || starts(a, "STU ")) &&
                strncmp(a + 4, "D,", 2))
               continue;
            dval = NULL;
         }
      }
      /* compact */
      {
         int j = 0;
         for (i = 0; i < lines.len; i++)
            if (*(char *)lines.data[i]) lines.data[j++] = lines.data[i];
         lines.len = j;
      }
   }
   for (i = 0; i < lines.len; i++) total += strlen(lines.data[i]) + 1;
   res = xmalloc(total + 1);
   p = res;
   for (i = 0; i < lines.len; i++)
   {
      size_t l = strlen(lines.data[i]);
      memcpy(p, lines.data[i], l);
      p += l;
      *p++ = '\n';
   }
   *p = 0;
   return res;
}

/* ------------------------------------------------------------------ */
/* data                                                                */
/* ------------------------------------------------------------------ */

static void emit_data(Obj *var)
{
   int size = var->ty->size, pos = 0;
   Relocation *rel = var->rel;
   unsigned char *d = (unsigned char *)var->init_data;
   emit_label(var->asmname);
   while (pos < size)
   {
      if (rel && rel->offset == pos)
      {
         if (rel->addend) emit("FDB %s%+ld", *rel->label, rel->addend);
         else emit("FDB %s", *rel->label);
         rel = rel->next;
         pos += 2;
         continue;
      }
      {
         char line[256];
         int n = 0, len = 0;
         while (pos < size && n < 16 && !(rel && rel->offset == pos))
         {
            len += sprintf(line + len, "%s%d", n ? "," : "", d[pos]);
            pos++;
            n++;
         }
         emit("FCB %s", line);
      }
   }
}

/* ------------------------------------------------------------------ */
/* program                                                             */
/* ------------------------------------------------------------------ */

typedef struct Unit {
   struct Unit *next;
   Obj  *obj;
   char *text;        /* generated code (functions / asm) */
   size_t textlen;
   Vec   defs;        /* labels defined */
   int   reachable;
   int   is_data;
   int   is_bss;
   int   uses_float;
   int   is_hook;
} Unit;

static HashMap label_units;   /* asm label -> Unit */

static void scan_defs(Unit *u, const char *text)
{
   const char *p = text;
   while (*p)
   {
      if (isalpha((unsigned char)*p) || *p == '_')
      {
         const char *s = p;
         while (isalnum((unsigned char)*p) || *p == '_' || *p == '$' || *p == '.') p++;
         {
            char *name = xstrndup(s, p - s);
            vec_push(&u->defs, name);
            if (!hm_get(&label_units, name)) hm_put(&label_units, name, u);
         }
      }
      while (*p && *p != '\n') p++;
      if (*p) p++;
   }
}

static void mark(Unit *u);

static void scan_refs(const char *text)
{
   const char *p = text;
   while (*p)
   {
      if (*p == ';' || (*p == '*' && (p == text || p[-1] == '\n')))
      {
         while (*p && *p != '\n') p++;
         continue;
      }
      if (*p == '"')
      {
         p++;
         while (*p && *p != '"' && *p != '\n') p++;
         if (*p) p++;
         continue;
      }
      if (isalpha((unsigned char)*p) || *p == '_')
      {
         const char *s = p;
         Unit *u;
         while (isalnum((unsigned char)*p) || *p == '_' || *p == '$' || *p == '.') p++;
         u = hm_get2(&label_units, s, (int)(p - s));
         if (u) mark(u);
         continue;
      }
      p++;
   }
}

static void mark(Unit *u)
{
   if (u->reachable) return;
   u->reachable = 1;
   if (u->text) scan_refs(u->text);
   if (u->obj && u->obj->rel)
   {
      Relocation *r;
      for (r = u->obj->rel; r; r = r->next)
      {
         Unit *t = hm_get(&label_units, *r->label);
         if (t) mark(t);
      }
   }
}

void codegen(Obj *prog, FILE *outfile, GenOpts *opts)
{
   Obj *var;
   Unit head, *cur = &head, *u;
   HashMap defined;
   char *buf;
   FILE *saved;
   memset(&defined, 0, sizeof(defined));
   memset(&label_units, 0, sizeof(label_units));
   head.next = NULL;

   /* 1. generate code of every function and asm block into units */
   for (var = prog; var; var = var->next)
   {
      if (var->asmtext)
      {
         u = xcalloc(sizeof(Unit));
         u->obj = var;
         u->text = var->asmtext;
         scan_defs(u, var->asmtext);
         cur = cur->next = u;
         continue;
      }
      if (!var->is_function || !var->is_definition) continue;
      if (hm_get(&defined, var->asmname)) error("duplicate definition of function %s", var->name);
      hm_put(&defined, var->asmname, var);
      u = xcalloc(sizeof(Unit));
      u->obj = var;
      saved = out;
      out = mem_open();
      fn_float = 0;
      gen_function(var);
      u->uses_float = fn_float;
      buf = mem_close(out);
      out = saved;
      u->text = (opt_level > 0 && var->asms.len == 0) ? peephole(buf) : buf;
      hm_put(&label_units, var->asmname, u);
      vec_push(&u->defs, var->asmname);
      cur = cur->next = u;
   }
   /* 2. data: initialized definitions first, then tentative */
   for (var = prog; var; var = var->next)
   {
      if (var->is_function || var->asmtext || var->ty->kind == TY_VOID) continue;
      if (!var->init_data) continue;
      if (hm_get(&defined, var->asmname))
      {
         Obj *o = hm_get(&defined, var->asmname);
         if (o->init_data && o != var) error("multiple definition of %s", var->name);
         continue;
      }
      hm_put(&defined, var->asmname, var);
      u = xcalloc(sizeof(Unit));
      u->obj = var;
      u->is_data = 1;
      hm_put(&label_units, var->asmname, u);
      cur = cur->next = u;
   }
   for (var = prog; var; var = var->next)
   {
      if (var->is_function || var->asmtext || var->ty->kind == TY_VOID) continue;
      if (var->init_data || var->is_extern) continue;
      if (hm_get(&defined, var->asmname))
      {
         Obj *o = hm_get(&defined, var->asmname);
         if (o->ty->size < var->ty->size && !o->init_data) o->ty = var->ty;
         continue;
      }
      if (var->ty->kind == TY_ARRAY && var->ty->array_len < 0)
      {
         /* int a[]; -> one element */
         var->ty = array_of(var->ty->base, 1);
      }
      hm_put(&defined, var->asmname, var);
      u = xcalloc(sizeof(Unit));
      u->obj = var;
      u->is_bss = 1;
      hm_put(&label_units, var->asmname, u);
      cur = cur->next = u;
   }

   /* hook pointers: defined by the code generator */
   {
      int h;
      for (h = 0; h < NHOOKS; h++)
      {
         Unit *hu = xcalloc(sizeof(Unit));
         hu->is_hook = 1;
         hu->text = NULL;
         vec_push(&hu->defs, (char *)hooks[h][0]);
         hm_put(&label_units, hooks[h][0], hu);
         hu->next = NULL;
         cur = cur->next = hu;
      }
   }

   /* 3. reachability from the start code */
   u = hm_get(&label_units, "__start");
   if (!u) error("startup code (__start) not found, library missing?");
   mark(u);
   {
      int float_used = 0, h;
      for (u = head.next; u; u = u->next) if (u->reachable && u->uses_float) float_used = 1;
      for (h = 0; h < NHOOKS; h++)
      {
         Unit *hu = hm_get(&label_units, hooks[h][0]);
         Unit *impl = hm_get(&label_units, hooks[h][1]);
         if (hu->reachable && float_used && impl) mark(impl);
         hu->obj = (Obj *)(impl && impl->reachable ? hooks[h][1] : NULL);
      }
   }

   /* undefined references: C symbols (leading '_') without a definition */
   {
      int errors = 0;
      HashMap seen;
      memset(&seen, 0, sizeof(seen));
      for (u = head.next; u; u = u->next)
      {
         const char *p;
         if (!u->reachable || !u->text) continue;
         for (p = u->text; *p;)
         {
            if (*p == ';' || (*p == '*' && (p == u->text || p[-1] == '\n')))
            {
               while (*p && *p != '\n') p++;
               continue;
            }
            if (*p == '"')
            {
               p++;
               while (*p && *p != '"' && *p != '\n') p++;
               if (*p) p++;
               continue;
            }
            if ((*p == '_') && (p == u->text || !(isalnum((unsigned char)p[-1]) || p[-1] == '$' || p[-1] == '.')))
            {
               const char *st = p;
               char *name;
               while (isalnum((unsigned char)*p) || *p == '_' || *p == '$' || *p == '.') p++;
               name = xstrndup(st, p - st);
               if (!hm_get(&label_units, name) && strcmp(name, "__bss") && strcmp(name, "__end") &&
                   strcmp(name, "__eop") && !hm_get(&seen, name))
               {
                  hm_put(&seen, name, (void *)1);
                  fprintf(stderr, "cc9: error: undefined reference to '%s'\n", name + 1);
                  errors++;
               }
               continue;
            }
            if (isalnum((unsigned char)*p) || *p == '$' || *p == '.')
            {
               while (isalnum((unsigned char)*p) || *p == '_' || *p == '$' || *p == '.') p++;
               continue;
            }
            p++;
         }
      }
      if (errors) exit(1);
   }

   /* 4. output */
   out = outfile;
   fprintf(out, "* generated by cc9 %s - ANSI C compiler for the Sparrow09\n", CC9_VERSION);
   emit("CPU = 6309");
   fprintf(out, "ORG_ = $%04lX\n", opts->org);
   fprintf(out, "STACK_TOP = $%04lX\n", opts->stack_top);
   if (opts->header)
   {
      emit("ORG ORG_-8");
      emit("STORE ORG_-8,__eop-ORG_+8,\"%s\",bin", opts->outname);
      emit("BYTE 'S',0");
      emit("WORD ORG_");
      emit("WORD __eop-ORG_");
      emit("WORD 0");
      /* BASIC line: 2026 SYS $xxxx */
      emit("WORD 0");
      emit("BYTE 12");
      emit("WORD 2026");
      emit("BYTE $A1");
      emit("BYTE \" $%04lX\",0", opts->org + 15);
      emit("BYTE 12");
      emit("BYTE 0");
   }
   else
   {
      emit("ORG ORG_");
      emit("STORE ORG_,__eop-ORG_,\"%s\",bin", opts->outname);
   }
   /* the startup code must come first: it is the SYS entry point */
   {
      Unit *su = hm_get(&label_units, "__start");
      fputs(su->text, out);
      su->is_hook = 2;
   }
   for (u = head.next; u; u = u->next)
   {
      if (!u->reachable || u->is_data || u->is_bss || u->is_hook) continue;
      fputs(u->text, out);
      if (u->text[0] && u->text[strlen(u->text) - 1] != '\n') fputc('\n', out);
   }
   fprintf(out, "\n* initialized data\n");
   for (u = head.next; u; u = u->next)
      if (u->reachable && u->is_data) emit_data(u->obj);
   for (u = head.next; u; u = u->next)
      if (u->reachable && u->is_hook == 1)
      {
         emit_label(u->defs.data[0]);
         emit("FDB %s", u->obj ? (char *)u->obj : "0");
      }
   {
      int i;
      Unit *u2;
      for (i = 0; i < dconst_list.len; i++)
      {
         char *l = dconst_list.data[i];
         unsigned long long u = strtoull(l + 3, NULL, 16);
         int k, used = 0;
         for (u2 = head.next; u2 && !used; u2 = u2->next)
            if (u2->reachable && u2->text && !u2->is_data && strstr(u2->text, l)) used = 1;
         if (!used) continue;
         emit_label(l);
         fprintf(out, "        FCB ");
         for (k = 7; k >= 0; k--) fprintf(out, "%d%s", (int)((u >> (k * 8)) & 0xff), k ? "," : "\n");
      }
   }
   emit_label("__eop");
   fprintf(out, "\n* uninitialized data\n");
   emit_label("__bss");
   for (u = head.next; u; u = u->next)
   {
      if (!u->reachable || !u->is_bss) continue;
      emit_label(u->obj->asmname);
      if (u->obj->ty->size > 0) emit("RMB %d", u->obj->ty->size);
   }
   emit_label("__end");
   emit("END");
}
