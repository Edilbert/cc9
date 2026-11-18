#include "cc9.h"

/* Sparrow09 / 6309: big endian, no alignment requirements */
static Type t_void    = { TY_VOID,    1, 1, 0 };
static Type t_char    = { TY_CHAR,    1, 1, 0 };
static Type t_schar   = { TY_CHAR,    1, 1, 0 };
static Type t_uchar   = { TY_CHAR,    1, 1, 1 };
static Type t_short   = { TY_SHORT,   2, 1, 0 };
static Type t_ushort  = { TY_SHORT,   2, 1, 1 };
static Type t_int     = { TY_INT,     2, 1, 0 };
static Type t_uint    = { TY_INT,     2, 1, 1 };
static Type t_long    = { TY_LONG,    4, 1, 0 };
static Type t_ulong   = { TY_LONG,    4, 1, 1 };
static Type t_float   = { TY_FLOAT,   4, 1, 0 };
static Type t_double  = { TY_DOUBLE,  8, 1, 0 };
static Type t_ldouble = { TY_LDOUBLE, 8, 1, 0 };

Type *ty_void = &t_void, *ty_char = &t_char, *ty_schar = &t_schar,
     *ty_uchar = &t_uchar, *ty_short = &t_short, *ty_ushort = &t_ushort,
     *ty_int = &t_int, *ty_uint = &t_uint, *ty_long = &t_long,
     *ty_ulong = &t_ulong, *ty_float = &t_float, *ty_double = &t_double,
     *ty_ldouble = &t_ldouble;

int is_char_signed = 1;

int is_integer(Type *ty)
{
   TypeKind k = ty->kind;
   return k == TY_CHAR || k == TY_SHORT || k == TY_INT || k == TY_LONG || k == TY_ENUM;
}

int is_flonum(Type *ty)
{
   return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE;
}

int is_numeric(Type *ty) { return is_integer(ty) || is_flonum(ty); }

int is_scalar(Type *ty) { return is_numeric(ty) || ty->kind == TY_PTR; }

int is_compatible(Type *t1, Type *t2)
{
   if (t1 == t2) return 1;
   if (t1->origin) return is_compatible(t1->origin, t2);
   if (t2->origin) return is_compatible(t1, t2->origin);
   if (t1->kind != t2->kind)
   {
      /* enum is compatible with int */
      if ((t1->kind == TY_ENUM && t2->kind == TY_INT && !t2->is_unsigned) ||
          (t2->kind == TY_ENUM && t1->kind == TY_INT && !t1->is_unsigned)) return 1;
      return 0;
   }
   switch (t1->kind)
   {
      case TY_CHAR: case TY_SHORT: case TY_INT: case TY_LONG:
         return t1->is_unsigned == t2->is_unsigned;
      case TY_FLOAT: case TY_DOUBLE: case TY_LDOUBLE: case TY_VOID:
         return 1;
      case TY_PTR:
         return is_compatible(t1->base, t2->base);
      case TY_FUNC:
      {
         Type *p1, *p2;
         if (!is_compatible(t1->return_ty, t2->return_ty)) return 0;
         if (!t1->has_proto || !t2->has_proto) return 1;
         if (t1->is_variadic != t2->is_variadic) return 0;
         for (p1 = t1->params, p2 = t2->params; p1 && p2; p1 = p1->next, p2 = p2->next)
            if (!is_compatible(p1, p2)) return 0;
         return p1 == NULL && p2 == NULL;
      }
      case TY_ARRAY:
         if (!is_compatible(t1->base, t2->base)) return 0;
         return t1->array_len < 0 || t2->array_len < 0 || t1->array_len == t2->array_len;
      default:
         return 0;
   }
}

Type *copy_type(Type *ty)
{
   Type *ret = xmalloc(sizeof(Type));
   *ret = *ty;
   ret->origin = ty;
   return ret;
}

Type *pointer_to(Type *base)
{
   Type *ty = xcalloc(sizeof(Type));
   ty->kind = TY_PTR;
   ty->size = 2;
   ty->align = 1;
   ty->base = base;
   ty->is_unsigned = 1;
   return ty;
}

Type *func_type(Type *return_ty)
{
   Type *ty = xcalloc(sizeof(Type));
   ty->kind = TY_FUNC;
   ty->size = 1;
   ty->align = 1;
   ty->return_ty = return_ty;
   return ty;
}

Type *array_of(Type *base, int len)
{
   Type *ty = xcalloc(sizeof(Type));
   ty->kind = TY_ARRAY;
   ty->size = len < 0 ? 0 : base->size * len;
   ty->align = 1;
   ty->base = base;
   ty->array_len = len;
   return ty;
}

Type *enum_type(void)
{
   Type *ty = xcalloc(sizeof(Type));
   ty->kind = TY_ENUM;
   ty->size = 2;
   ty->align = 1;
   return ty;
}

Type *struct_type(void)
{
   Type *ty = xcalloc(sizeof(Type));
   ty->kind = TY_STRUCT;
   ty->size = 0;
   ty->align = 1;
   return ty;
}

/* integer promotion */
static Type *promote(Type *ty)
{
   if (ty->kind == TY_CHAR) return ty_int;
   if (ty->kind == TY_SHORT) return ty->is_unsigned ? ty_uint : ty_int;
   if (ty->kind == TY_ENUM) return ty_int;
   return ty;
}

static Type *get_common_type(Type *ty1, Type *ty2)
{
   if (ty1->base) return pointer_to(ty1->base);
   if (ty1->kind == TY_FUNC) return pointer_to(ty1);
   if (ty2->kind == TY_FUNC) return pointer_to(ty2);
   if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE) return ty_ldouble;
   if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE) return ty_double;
   if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT) return ty_float;
   ty1 = promote(ty1);
   ty2 = promote(ty2);
   if (ty1->kind == TY_LONG && ty1->is_unsigned) return ty_ulong;
   if (ty2->kind == TY_LONG && ty2->is_unsigned) return ty_ulong;
   if (ty1->kind == TY_LONG || ty2->kind == TY_LONG) return ty_long;
   if (ty1->is_unsigned || ty2->is_unsigned) return ty_uint;
   return ty_int;
}

static void usual_arith_conv(Node **lhs, Node **rhs)
{
   Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
   *lhs = new_cast(*lhs, ty);
   *rhs = new_cast(*rhs, ty);
}

static void int_promote(Node **n)
{
   Type *ty = promote((*n)->ty);
   if (ty != (*n)->ty) *n = new_cast(*n, ty);
}

void add_type(Node *node)
{
   Node *n;
   if (!node || node->ty) return;
   add_type(node->lhs);
   add_type(node->rhs);
   add_type(node->cond);
   add_type(node->then);
   add_type(node->els);
   add_type(node->init);
   add_type(node->inc);
   for (n = node->body; n; n = n->next) add_type(n);
   for (n = node->args; n; n = n->next) add_type(n);

   switch (node->kind)
   {
      case ND_NUM:
         node->ty = ty_int;
         return;
      case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
      case ND_BITAND: case ND_BITOR: case ND_BITXOR:
         usual_arith_conv(&node->lhs, &node->rhs);
         node->ty = node->lhs->ty;
         return;
      case ND_NEG:
      {
         Type *ty = get_common_type(ty_int, node->lhs->ty);
         node->lhs = new_cast(node->lhs, ty);
         node->ty = ty;
         return;
      }
      case ND_ASSIGN:
         if (node->lhs->ty->kind == TY_ARRAY) error_tok(node->lhs->tok, "not an lvalue");
         if (node->lhs->ty->kind != TY_STRUCT && node->lhs->ty->kind != TY_UNION)
            node->rhs = new_cast(node->rhs, node->lhs->ty);
         node->ty = node->lhs->ty;
         return;
      case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
         usual_arith_conv(&node->lhs, &node->rhs);
         node->ty = ty_int;
         return;
      case ND_FUNCALL:
         node->ty = node->func_ty->return_ty;
         return;
      case ND_NOT: case ND_LOGOR: case ND_LOGAND:
         node->ty = ty_int;
         return;
      case ND_BITNOT:
         int_promote(&node->lhs);
         node->ty = node->lhs->ty;
         return;
      case ND_SHL: case ND_SHR:
         int_promote(&node->lhs);
         int_promote(&node->rhs);
         node->ty = node->lhs->ty;
         return;
      case ND_VAR:
         node->ty = node->var->ty;
         return;
      case ND_COND:
         if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID)
            node->ty = ty_void;
         else if ((node->then->ty->kind == TY_STRUCT || node->then->ty->kind == TY_UNION))
            node->ty = node->then->ty;
         else if (node->then->ty->kind == TY_PTR || node->then->ty->kind == TY_ARRAY ||
                  node->then->ty->kind == TY_FUNC)
         {
            Type *b = node->then->ty->kind == TY_FUNC ? node->then->ty : node->then->ty->base;
            /* void * vs T*: result is void*;  ptr vs 0: ptr */
            if ((node->els->ty->kind == TY_PTR || node->els->ty->kind == TY_ARRAY) &&
                node->els->ty->base->kind == TY_VOID)
               node->ty = node->els->ty->kind == TY_PTR ? node->els->ty : pointer_to(ty_void);
            else node->ty = pointer_to(b);
            node->then = new_cast(node->then, node->ty);
            node->els = new_cast(node->els, node->ty);
         }
         else if (node->els->ty->kind == TY_PTR || node->els->ty->kind == TY_ARRAY ||
                  node->els->ty->kind == TY_FUNC)
         {
            Type *b = node->els->ty->kind == TY_FUNC ? node->els->ty : node->els->ty->base;
            node->ty = pointer_to(b);
            node->then = new_cast(node->then, node->ty);
            node->els = new_cast(node->els, node->ty);
         }
         else
         {
            usual_arith_conv(&node->then, &node->els);
            node->ty = node->then->ty;
         }
         return;
      case ND_COMMA:
         node->ty = node->rhs->ty;
         return;
      case ND_MEMBER:
         node->ty = node->member->ty;
         return;
      case ND_ADDR:
      {
         Type *ty = node->lhs->ty;
         if (ty->kind == TY_ARRAY) node->ty = pointer_to(ty->base);
         else node->ty = pointer_to(ty);
         return;
      }
      case ND_DEREF:
         if (node->lhs->ty->kind == TY_FUNC) { node->ty = node->lhs->ty; return; }
         if (!node->lhs->ty->base) error_tok(node->tok, "invalid pointer dereference");
         if (node->lhs->ty->base->kind == TY_VOID)
            error_tok(node->tok, "dereferencing a void pointer");
         node->ty = node->lhs->ty->base;
         return;
      default:
         node->ty = ty_void;
         return;
   }
}
