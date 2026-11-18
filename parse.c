/*
 * C89 parser and semantic analysis (recursive descent, after chibicc)
 */
#include "cc9.h"

typedef struct {
   Obj  *var;
   Type *type_def;
   Type *enum_ty;
   int   enum_val;
} VarScope;

typedef struct Scope {
   struct Scope *next;
   HashMap vars;
   HashMap tags;
} Scope;

typedef struct {
   int is_typedef, is_static, is_extern, is_register;
} VarAttr;

typedef struct Initializer {
   struct Initializer *next;
   Type *ty;
   Token *tok;
   int   is_flexible;
   Node *expr;
   struct Initializer **children;
   Member *mem;   /* union: initialized member */
} Initializer;

typedef struct InitDesg {
   struct InitDesg *next;
   int     idx;
   Member *member;
   Obj    *var;
} InitDesg;

static Obj   *locals;
static Obj   *globals;
Obj          *all_globals;
static Scope *scope;
static Obj   *current_fn;
static Node  *gotos;
static Node  *labels;
static char  *brk_label;
static char  *cont_label;
static Node  *current_switch;
static int    static_count;

static int is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename_(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void initializer2(Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static void gvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static long long eval(Node *node);
static long long eval2(Node *node, char ***label);
static long long eval_rval(Node *node, char ***label);
static int is_const_expr(Node *node);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static double eval_double(Node *node);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor_(Token **rest, Token *tok);
static Node *bitxor_(Token **rest, Token *tok);
static Node *bitand_(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok, Node *node);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);
static int is_function(Token *tok);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);
Type *get_common_type_pub(Type *ty);
int parse_file_no;

static int align_to(int n, int align) { return (n + align - 1) / align * align; }

static void enter_scope(void)
{
   Scope *sc = xcalloc(sizeof(Scope));
   sc->next = scope;
   scope = sc;
}

static void leave_scope(void) { scope = scope->next; }

static VarScope *find_var(Token *tok)
{
   Scope *sc;
   for (sc = scope; sc; sc = sc->next)
   {
      VarScope *sc2 = hm_get2(&sc->vars, tok->loc, tok->len);
      if (sc2) return sc2;
   }
   return NULL;
}

static Type *find_tag(Token *tok)
{
   Scope *sc;
   for (sc = scope; sc; sc = sc->next)
   {
      Type *ty = hm_get2(&sc->tags, tok->loc, tok->len);
      if (ty) return ty;
   }
   return NULL;
}

static Node *new_node(NodeKind kind, Token *tok)
{
   Node *node = xcalloc(sizeof(Node));
   node->kind = kind;
   node->tok = tok;
   return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok)
{
   Node *node = new_node(kind, tok);
   node->lhs = lhs;
   node->rhs = rhs;
   return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok)
{
   Node *node = new_node(kind, tok);
   node->lhs = expr;
   return node;
}

static Node *new_num(long long val, Token *tok)
{
   Node *node = new_node(ND_NUM, tok);
   node->val = val;
   return node;
}

static Node *new_long(long long val, Token *tok)
{
   Node *node = new_node(ND_NUM, tok);
   node->val = val;
   node->ty = ty_long;
   return node;
}

static Node *new_uint(long long val, Token *tok)
{
   Node *node = new_node(ND_NUM, tok);
   node->val = val;
   node->ty = ty_uint;
   return node;
}

static Node *new_var_node(Obj *var, Token *tok)
{
   Node *node = new_node(ND_VAR, tok);
   node->var = var;
   if (!var->is_local && current_fn) vec_push(&current_fn->refs, var);
   return node;
}

Node *new_cast(Node *expr, Type *ty)
{
   Node *node;
   add_type(expr);
   node = xcalloc(sizeof(Node));
   node->kind = ND_CAST;
   node->tok = expr->tok;
   node->lhs = expr;
   node->ty = copy_type(ty);
   return node;
}

static VarScope *push_scope(char *name)
{
   VarScope *sc = xcalloc(sizeof(VarScope));
   hm_put(&scope->vars, name, sc);
   return sc;
}

static Initializer *new_initializer(Type *ty, int is_flexible)
{
   Initializer *init = xcalloc(sizeof(Initializer));
   init->ty = ty;
   if (ty->kind == TY_ARRAY)
   {
      int i;
      if (is_flexible && ty->size == 0 && ty->array_len < 0)
      {
         init->is_flexible = 1;
         return init;
      }
      init->children = xcalloc(sizeof(Initializer *) * (ty->array_len > 0 ? ty->array_len : 1));
      for (i = 0; i < ty->array_len; i++) init->children[i] = new_initializer(ty->base, 0);
      return init;
   }
   if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
   {
      int len = 0;
      Member *mem;
      for (mem = ty->members; mem; mem = mem->next) len++;
      init->children = xcalloc(sizeof(Initializer *) * (len ? len : 1));
      for (mem = ty->members; mem; mem = mem->next)
      {
         if (is_flexible && ty->is_flexible && !mem->next)
         {
            Initializer *child = xcalloc(sizeof(Initializer));
            child->ty = mem->ty;
            child->is_flexible = 1;
            init->children[mem->idx] = child;
         }
         else init->children[mem->idx] = new_initializer(mem->ty, 0);
      }
      return init;
   }
   return init;
}

static Obj *new_var(char *name, Type *ty)
{
   Obj *var = xcalloc(sizeof(Obj));
   var->name = name;
   var->ty = ty;
   push_scope(name)->var = var;
   return var;
}

static Obj *new_lvar(char *name, Type *ty)
{
   Obj *var = new_var(name, ty);
   var->is_local = 1;
   var->next = locals;
   locals = var;
   return var;
}

static Obj *new_gvar(char *name, Type *ty)
{
   Obj *var = new_var(name, ty);
   var->next = globals;
   var->is_definition = 1;
   var->file_no = parse_file_no;
   var->asmname = format("_%s", name);
   globals = var;
   return var;
}

static char *new_unique_name(void)
{
   static int id;
   return format("L$%d", id++);
}

static Obj *new_anon_gvar(Type *ty)
{
   Obj *v = new_gvar(new_unique_name(), ty);
   v->asmname = v->name;
   v->is_static = 1;
   return v;
}

static Obj *new_string_literal(char *p, int len, Type *ty)
{
   Obj *var = new_anon_gvar(ty);
   var->init_data = xmalloc(len);
   memcpy(var->init_data, p, len);
   if (current_fn) vec_push(&current_fn->refs, var);
   return var;
}

static char *get_ident(Token *tok)
{
   if (tok->kind != TK_IDENT) error_tok(tok, "expected an identifier");
   return xstrndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok)
{
   if (tok->kind == TK_IDENT)
   {
      VarScope *sc = find_var(tok);
      if (sc) return sc->type_def;
   }
   return NULL;
}

static void push_tag_scope(Token *tok, Type *ty)
{
   hm_put2(&scope->tags, tok->loc, tok->len, ty);
}

/* declspec = ("void" | "char" | ... | typedef-name | struct | union | enum
 *             | "typedef" | "static" | "extern" | "auto" | "register"
 *             | "const" | "volatile" | "signed" | "unsigned")+          */
static Type *declspec(Token **rest, Token *tok, VarAttr *attr)
{
   enum {
      VOID = 1 << 0, CHAR = 1 << 2, SHORT = 1 << 4, INT = 1 << 6,
      LONG = 1 << 8, FLOAT = 1 << 10, DOUBLE = 1 << 12, OTHER = 1 << 14,
      SIGNED = 1 << 16, UNSIGNED = 1 << 17
   };
   Type *ty = ty_int;
   int counter = 0, is_const = 0, is_volatile = 0;

   while (is_typename(tok))
   {
      if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern") ||
          equal(tok, "auto") || equal(tok, "register") || equal(tok, "inline") ||
          equal(tok, "__inline"))
      {
         if (!attr) error_tok(tok, "storage class specifier is not allowed in this context");
         if (equal(tok, "typedef")) attr->is_typedef = 1;
         else if (equal(tok, "static")) attr->is_static = 1;
         else if (equal(tok, "extern")) attr->is_extern = 1;
         else if (equal(tok, "register")) attr->is_register = 1;
         if (attr->is_typedef + attr->is_static + attr->is_extern > 1)
            error_tok(tok, "typedef, static and extern may not be used together");
         tok = tok->next;
         continue;
      }
      if (equal(tok, "const") || equal(tok, "__const")) { is_const = 1; tok = tok->next; continue; }
      if (equal(tok, "volatile") || equal(tok, "__volatile__"))
      { is_volatile = 1; tok = tok->next; continue; }

      {
         Type *ty2 = find_typedef(tok);
         if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") || ty2)
         {
            if (counter) break;
            if (equal(tok, "struct")) ty = struct_decl(&tok, tok->next);
            else if (equal(tok, "union")) ty = union_decl(&tok, tok->next);
            else if (equal(tok, "enum")) ty = enum_specifier(&tok, tok->next);
            else { ty = ty2; tok = tok->next; }
            counter += OTHER;
            continue;
         }
      }
      if (equal(tok, "void")) counter += VOID;
      else if (equal(tok, "char")) counter += CHAR;
      else if (equal(tok, "short")) counter += SHORT;
      else if (equal(tok, "int")) counter += INT;
      else if (equal(tok, "long")) counter += LONG;
      else if (equal(tok, "float")) counter += FLOAT;
      else if (equal(tok, "double")) counter += DOUBLE;
      else if (equal(tok, "signed") || equal(tok, "__signed__")) counter |= SIGNED;
      else if (equal(tok, "unsigned")) counter |= UNSIGNED;
      else error_tok(tok, "internal error: declspec");

      switch (counter)
      {
         case VOID: ty = ty_void; break;
         case CHAR: ty = ty_char; break;
         case SIGNED + CHAR: ty = ty_schar; break;
         case UNSIGNED + CHAR: ty = ty_uchar; break;
         case SHORT: case SHORT + INT: case SIGNED + SHORT: case SIGNED + SHORT + INT:
            ty = ty_short; break;
         case UNSIGNED + SHORT: case UNSIGNED + SHORT + INT: ty = ty_ushort; break;
         case INT: case SIGNED: case SIGNED + INT: ty = ty_int; break;
         case UNSIGNED: case UNSIGNED + INT: ty = ty_uint; break;
         case LONG: case LONG + INT: case SIGNED + LONG: case SIGNED + LONG + INT:
            ty = ty_long; break;
         case UNSIGNED + LONG: case UNSIGNED + LONG + INT: ty = ty_ulong; break;
         case LONG + LONG: case LONG + LONG + INT: case SIGNED + LONG + LONG:
         case SIGNED + LONG + LONG + INT:
            warn_tok(tok, "long long is not C89, using long (32 bit)");
            ty = ty_long; break;
         case UNSIGNED + LONG + LONG: case UNSIGNED + LONG + LONG + INT:
            warn_tok(tok, "long long is not C89, using unsigned long (32 bit)");
            ty = ty_ulong; break;
         case FLOAT: ty = ty_float; break;
         case DOUBLE: ty = ty_double; break;
         case LONG + DOUBLE: ty = ty_ldouble; break;
         default: error_tok(tok, "invalid type");
      }
      tok = tok->next;
   }
   if (is_const || is_volatile)
   {
      ty = copy_type(ty);
      ty->is_const = is_const;
      ty->is_volatile = is_volatile;
   }
   *rest = tok;
   return ty;
}

/* func-params = ("void" | param ("," param)* ("," "...")?)? ")"
 *             | ident ("," ident)* ")"      (old style)            */
static Type *func_params(Token **rest, Token *tok, Type *ty)
{
   Type head, *cur = &head;
   int is_variadic = 0;
   head.next = NULL;

   if (equal(tok, "void") && equal(tok->next, ")"))
   {
      *rest = tok->next->next;
      ty = func_type(ty);
      ty->has_proto = 1;
      return ty;
   }
   if (equal(tok, ")"))
   {
      *rest = tok->next;
      ty = func_type(ty);
      ty->has_proto = 0;
      return ty;
   }
   /* old style identifier list */
   if (tok->kind == TK_IDENT && !find_typedef(tok))
   {
      Type *fty = func_type(ty);
      fty->has_proto = 0;
      while (!equal(tok, ")"))
      {
         Type *t;
         if (cur != &head) tok = skip(tok, ",");
         if (tok->kind != TK_IDENT) error_tok(tok, "expected parameter name");
         t = copy_type(ty_int);
         t->name = tok;
         t->is_const = 2;    /* marker: implicit int (old style) */
         cur = cur->next = t;
         tok = tok->next;
      }
      *rest = tok->next;
      fty->params = head.next;
      fty->is_variadic = 2;  /* marker: identifier list */
      return fty;
   }
   while (!equal(tok, ")"))
   {
      Type *ty2;
      Token *name;
      if (cur != &head) tok = skip(tok, ",");
      if (equal(tok, "..."))
      {
         is_variadic = 1;
         tok = tok->next;
         skip(tok, ")");
         break;
      }
      ty2 = declspec(&tok, tok, NULL);
      ty2 = declarator(&tok, tok, ty2);
      name = ty2->name;
      if (ty2->kind == TY_ARRAY)
      {
         ty2 = pointer_to(ty2->base);
         ty2->name = name;
      }
      else if (ty2->kind == TY_FUNC)
      {
         ty2 = pointer_to(ty2);
         ty2->name = name;
      }
      cur = cur->next = copy_type(ty2);
   }
   ty = func_type(ty);
   ty->params = head.next;
   ty->is_variadic = is_variadic;
   ty->has_proto = 1;
   *rest = tok->next;
   return ty;
}

/* array-dimensions = const-expr? "]" type-suffix */
static Type *array_dimensions(Token **rest, Token *tok, Type *ty)
{
   long long sz;
   if (equal(tok, "]"))
   {
      ty = type_suffix(rest, tok->next, ty);
      return array_of(ty, -1);
   }
   sz = const_expr(&tok, tok);
   if (sz < 0) error_tok(tok, "negative array size");
   tok = skip(tok, "]");
   ty = type_suffix(rest, tok, ty);
   if (ty->size * sz > 0xffff) error_tok(tok, "array too large");
   return array_of(ty, (int)sz);
}

static Type *type_suffix(Token **rest, Token *tok, Type *ty)
{
   if (equal(tok, "(")) return func_params(rest, tok->next, ty);
   if (equal(tok, "[")) return array_dimensions(rest, tok->next, ty);
   *rest = tok;
   return ty;
}

static Type *pointers(Token **rest, Token *tok, Type *ty)
{
   while (consume(&tok, tok, "*"))
   {
      ty = pointer_to(ty);
      while (equal(tok, "const") || equal(tok, "volatile") || equal(tok, "__const"))
      {
         if (equal(tok, "const") || equal(tok, "__const")) ty->is_const = 1;
         else ty->is_volatile = 1;
         tok = tok->next;
      }
   }
   *rest = tok;
   return ty;
}

/* declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) type-suffix */
static Type *declarator(Token **rest, Token *tok, Type *ty)
{
   Token *name = NULL, *name_pos;
   ty = pointers(&tok, tok, ty);
   if (equal(tok, "(") && !is_typename(tok->next) && !equal(tok->next, ")"))
   {
      Token *start = tok;
      Type dummy, *ty2;
      memset(&dummy, 0, sizeof(dummy));
      declarator(&tok, start->next, &dummy);
      tok = skip(tok, ")");
      ty = type_suffix(rest, tok, ty);
      ty2 = declarator(&tok, start->next, ty);
      return ty2;
   }
   name_pos = tok;
   if (tok->kind == TK_IDENT) { name = tok; tok = tok->next; }
   ty = type_suffix(rest, tok, ty);
   ty->name = name;
   (void)name_pos;
   return ty;
}

static Type *abstract_declarator(Token **rest, Token *tok, Type *ty)
{
   ty = pointers(&tok, tok, ty);
   if (equal(tok, "(") && !is_typename(tok->next) && !equal(tok->next, ")"))
   {
      Token *start = tok;
      Type dummy;
      memset(&dummy, 0, sizeof(dummy));
      abstract_declarator(&tok, start->next, &dummy);
      tok = skip(tok, ")");
      ty = type_suffix(rest, tok, ty);
      return abstract_declarator(&tok, start->next, ty);
   }
   return type_suffix(rest, tok, ty);
}

static Type *typename_(Token **rest, Token *tok)
{
   Type *ty = declspec(&tok, tok, NULL);
   return abstract_declarator(rest, tok, ty);
}

static int is_end(Token *tok)
{
   return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static int consume_end(Token **rest, Token *tok)
{
   if (equal(tok, "}")) { *rest = tok->next; return 1; }
   if (equal(tok, ",") && equal(tok->next, "}")) { *rest = tok->next->next; return 1; }
   return 0;
}

static Type *enum_specifier(Token **rest, Token *tok)
{
   Type *ty = enum_type();
   Token *tag = NULL;
   int i = 0, val = 0;
   if (tok->kind == TK_IDENT) { tag = tok; tok = tok->next; }
   if (tag && !equal(tok, "{"))
   {
      Type *t = find_tag(tag);
      if (!t)
      {
         /* forward reference to enum: treat as int */
         push_tag_scope(tag, ty);
         *rest = tok;
         return ty;
      }
      if (t->kind != TY_ENUM) error_tok(tag, "not an enum tag");
      *rest = tok;
      return t;
   }
   tok = skip(tok, "{");
   while (!consume_end(rest, tok))
   {
      char *name;
      VarScope *sc;
      if (i++ > 0) tok = skip(tok, ",");
      name = get_ident(tok);
      tok = tok->next;
      if (equal(tok, "=")) val = (int)const_expr(&tok, tok->next);
      sc = push_scope(name);
      sc->enum_ty = ty;
      if (val < 0) ty->is_variadic = 1;   /* enum has negative values */
      sc->enum_val = val++;
   }
   if (tag) push_tag_scope(tag, ty);
   return ty;
}

static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr)
{
   Node head, *cur = &head;
   int i = 0;
   head.next = NULL;
   while (!equal(tok, ";"))
   {
      Type *ty;
      Obj *var;
      if (i++ > 0)
      {
         if (!equal(tok, ",")) error_tok(tok, "expected ';' or ','");
         tok = tok->next;
      }
      ty = declarator(&tok, tok, basety);
      if (ty->kind == TY_VOID) error_tok(tok, "variable declared void");
      if (!ty->name) error_tok(tok, "variable name omitted");

      if (ty->kind == TY_FUNC || (attr && attr->is_extern))
      {
         /* block scope function / extern declaration: refer to global */
         char *name = get_ident(ty->name);
         Obj *g = NULL, *o;
         VarScope *sc;
         for (o = all_globals; o; o = o->next)
            if (!strcmp(o->name, name) && !o->is_static) { g = o; break; }
         if (!g)
         {
            for (o = globals; o; o = o->next) if (!strcmp(o->name, name)) { g = o; break; }
         }
         if (!g)
         {
            g = xcalloc(sizeof(Obj));
            g->name = name;
            g->ty = ty;
            g->asmname = format("_%s", name);
            g->is_function = ty->kind == TY_FUNC;
            g->is_extern = 1;
            g->next = globals;
            globals = g;
         }
         sc = push_scope(name);
         sc->var = g;
         continue;
      }

      if (attr && attr->is_static)
      {
         Obj *v = new_anon_gvar(ty);
         v->asmname = format("S$%d_%s", static_count++, get_ident(ty->name));
         push_scope(get_ident(ty->name))->var = v;
         if (equal(tok, "=")) gvar_initializer(&tok, tok->next, v);
         vec_push(&current_fn->refs, v);
         continue;
      }

      var = new_lvar(get_ident(ty->name), ty);
      var->tok = ty->name;
      if (equal(tok, "="))
      {
         Node *expr = lvar_initializer(&tok, tok->next, var);
         cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
      }
      if (var->ty->size < 0 || (var->ty->kind == TY_ARRAY && var->ty->array_len < 0))
         error_tok(ty->name, "variable has incomplete type");
      if (var->ty->kind == TY_STRUCT && var->ty->is_incomplete)
         error_tok(ty->name, "variable has incomplete type");
   }
   {
      Node *node = new_node(ND_BLOCK, tok);
      node->body = head.next;
      *rest = tok->next;
      return node;
   }
}

static Token *skip_excess_element(Token *tok)
{
   if (equal(tok, "{"))
   {
      tok = skip_excess_element(tok->next);
      return skip(tok, "}");
   }
   assign(&tok, tok);
   return tok;
}

static void string_initializer(Token **rest, Token *tok, Initializer *init)
{
   int len, i, esz;
   if (init->is_flexible)
      *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), 0);
   len = init->ty->array_len < tok->ty->array_len ? init->ty->array_len : tok->ty->array_len;
   esz = init->ty->base->size;
   for (i = 0; i < len; i++)
   {
      long v;
      if (esz == 1) v = (signed char)tok->str[i];
      else v = (short)(((unsigned char)tok->str[2*i] << 8) | (unsigned char)tok->str[2*i+1]);
      init->children[i]->expr = new_num(v, tok);
   }
   *rest = tok->next;
}

static int count_array_init_elements(Token *tok, Type *ty)
{
   Initializer *dummy = new_initializer(ty->base, 1);
   int i = 0;
   for (; !consume_end(&tok, tok); i++)
   {
      if (i > 0) tok = skip(tok, ",");
      initializer2(&tok, tok, dummy);
   }
   return i;
}

static void array_initializer1(Token **rest, Token *tok, Initializer *init)
{
   int i;
   tok = skip(tok, "{");
   if (init->is_flexible)
   {
      int len = count_array_init_elements(tok, init->ty);
      *init = *new_initializer(array_of(init->ty->base, len), 0);
   }
   for (i = 0; !consume_end(rest, tok); i++)
   {
      if (i > 0) tok = skip(tok, ",");
      if (i < init->ty->array_len) initializer2(&tok, tok, init->children[i]);
      else tok = skip_excess_element(tok);
   }
}

static void array_initializer2(Token **rest, Token *tok, Initializer *init)
{
   int i;
   if (init->is_flexible)
   {
      int len = count_array_init_elements(tok, init->ty);
      *init = *new_initializer(array_of(init->ty->base, len), 0);
   }
   for (i = 0; i < init->ty->array_len && !is_end(tok); i++)
   {
      if (i > 0) tok = skip(tok, ",");
      initializer2(&tok, tok, init->children[i]);
   }
   *rest = tok;
}

static void struct_initializer1(Token **rest, Token *tok, Initializer *init)
{
   Member *mem = init->ty->members;
   int first = 1;
   tok = skip(tok, "{");
   while (!consume_end(rest, tok))
   {
      if (!first) tok = skip(tok, ",");
      first = 0;
      if (mem)
      {
         initializer2(&tok, tok, init->children[mem->idx]);
         mem = mem->next;
      }
      else tok = skip_excess_element(tok);
   }
}

static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem)
{
   int first = 1;
   for (; mem && !is_end(tok); mem = mem->next)
   {
      if (!first) tok = skip(tok, ",");
      first = 0;
      initializer2(&tok, tok, init->children[mem->idx]);
   }
   *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init)
{
   init->mem = init->ty->members;
   if (equal(tok, "{"))
   {
      initializer2(&tok, tok->next, init->children[0]);
      consume(&tok, tok, ",");
      *rest = skip(tok, "}");
   }
   else initializer2(rest, tok, init->children[0]);
}

static void initializer2(Token **rest, Token *tok, Initializer *init)
{
   if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR)
   {
      string_initializer(rest, tok, init);
      return;
   }
   if (init->ty->kind == TY_ARRAY)
   {
      if (equal(tok, "{")) array_initializer1(rest, tok, init);
      else array_initializer2(rest, tok, init);
      return;
   }
   if (init->ty->kind == TY_STRUCT)
   {
      if (equal(tok, "{")) { struct_initializer1(rest, tok, init); return; }
      {
         Node *e = assign(rest, tok);
         add_type(e);
         if (e->ty->kind == TY_STRUCT) { init->expr = e; return; }
      }
      struct_initializer2(rest, tok, init, init->ty->members);
      return;
   }
   if (init->ty->kind == TY_UNION)
   {
      if (!equal(tok, "{"))
      {
         Token *t2;
         Node *e = assign(&t2, tok);
         add_type(e);
         if (e->ty->kind == TY_UNION) { init->expr = e; *rest = t2; return; }
      }
      union_initializer(rest, tok, init);
      return;
   }
   if (equal(tok, "{"))
   {
      /* scalar in braces: int x = {3}; */
      initializer2(&tok, tok->next, init);
      consume(&tok, tok, ",");
      *rest = skip(tok, "}");
      return;
   }
   init->expr = assign(rest, tok);
}

static Type *copy_struct_type(Type *ty)
{
   Member head, *cur = &head, *mem;
   head.next = NULL;
   ty = copy_type(ty);
   for (mem = ty->members; mem; mem = mem->next)
   {
      Member *m = xmalloc(sizeof(Member));
      *m = *mem;
      cur = cur->next = m;
   }
   ty->members = head.next;
   return ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty)
{
   Initializer *init = new_initializer(ty, 1);
   initializer2(rest, tok, init);
   if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible)
   {
      ty = copy_struct_type(ty);
      {
         Member *mem = ty->members;
         while (mem->next) mem = mem->next;
         mem->ty = init->children[mem->idx]->ty;
         ty->size += mem->ty->size;
      }
      *new_ty = ty;
      return init;
   }
   *new_ty = init->ty;
   return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok)
{
   if (desg->var) return new_var_node(desg->var, tok);
   if (desg->member)
   {
      Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
      node->member = desg->member;
      return node;
   }
   {
      Node *lhs = init_desg_expr(desg->next, tok);
      Node *rhs = new_num(desg->idx, tok);
      return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
   }
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok)
{
   if (ty->kind == TY_ARRAY)
   {
      Node *node = new_node(ND_NULL_EXPR, tok);
      int i;
      for (i = 0; i < ty->array_len; i++)
      {
         InitDesg desg2;
         Node *rhs;
         desg2.next = desg; desg2.idx = i; desg2.member = NULL; desg2.var = NULL;
         rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
         node = new_binary(ND_COMMA, node, rhs, tok);
      }
      return node;
   }
   if (ty->kind == TY_STRUCT && !init->expr)
   {
      Node *node = new_node(ND_NULL_EXPR, tok);
      Member *mem;
      for (mem = ty->members; mem; mem = mem->next)
      {
         InitDesg desg2;
         Node *rhs;
         desg2.next = desg; desg2.idx = 0; desg2.member = mem; desg2.var = NULL;
         rhs = create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
         node = new_binary(ND_COMMA, node, rhs, tok);
      }
      return node;
   }
   if (ty->kind == TY_UNION && !init->expr)
   {
      Member *mem = init->mem ? init->mem : ty->members;
      InitDesg desg2;
      desg2.next = desg; desg2.idx = 0; desg2.member = mem; desg2.var = NULL;
      return create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
   }
   if (!init->expr) return new_node(ND_NULL_EXPR, tok);
   {
      Node *lhs = init_desg_expr(desg, tok);
      return new_binary(ND_ASSIGN, lhs, init->expr, tok);
   }
}

static Node *lvar_initializer(Token **rest, Token *tok, Obj *var)
{
   Initializer *init = initializer(rest, tok, var->ty, &var->ty);
   InitDesg desg;
   Node *lhs, *rhs;
   desg.next = NULL; desg.idx = 0; desg.member = NULL; desg.var = var;
   lhs = new_node(ND_MEMZERO, tok);
   lhs->var = var;
   if (is_scalar(var->ty)) return create_lvar_init(init, var->ty, &desg, tok);
   rhs = create_lvar_init(init, var->ty, &desg, tok);
   return new_binary(ND_COMMA, lhs, rhs, tok);
}

/* big endian write */
static void write_buf(char *buf, unsigned long long val, int sz)
{
   int i;
   for (i = sz - 1; i >= 0; i--) { buf[i] = (char)(val & 0xff); val >>= 8; }
}

static void write_double(char *buf, double d, int size)
{
   if (size == 4)
   {
      float f = (float)d;
      uint32_t u;
      memcpy(&u, &f, 4);
      write_buf(buf, u, 4);
   }
   else
   {
      uint64_t u;
      memcpy(&u, &d, 8);
      write_buf(buf, u, 8);
   }
}

static Relocation *write_gvar_data(Relocation *cur, Initializer *init, Type *ty,
                                   char *buf, int offset)
{
   if (ty->kind == TY_ARRAY)
   {
      int sz = ty->base->size, i;
      for (i = 0; i < ty->array_len; i++)
         cur = write_gvar_data(cur, init->children[i], ty->base, buf, offset + sz * i);
      return cur;
   }
   if (ty->kind == TY_STRUCT)
   {
      Member *mem;
      if (init->expr) error_tok(init->expr->tok, "initializer element is not constant");
      for (mem = ty->members; mem; mem = mem->next)
      {
         if (mem->is_bitfield)
         {
            Node *e = init->children[mem->idx]->expr;
            if (e)
            {
               char *loc = buf + offset + mem->offset;
               unsigned v = ((unsigned char)loc[0] << 8) | (unsigned char)loc[1];
               unsigned mask = ((1u << mem->bit_width) - 1) << mem->bit_offset;
               unsigned x = (unsigned)eval(e);
               v = (v & ~mask) | ((x << mem->bit_offset) & mask);
               loc[0] = (char)(v >> 8);
               loc[1] = (char)v;
            }
         }
         else
            cur = write_gvar_data(cur, init->children[mem->idx], mem->ty, buf,
                                  offset + mem->offset);
      }
      return cur;
   }
   if (ty->kind == TY_UNION)
   {
      Member *mem = init->mem ? init->mem : ty->members;
      if (init->expr) error_tok(init->expr->tok, "initializer element is not constant");
      if (!mem) return cur;
      return write_gvar_data(cur, init->children[mem->idx], mem->ty, buf, offset);
   }
   if (!init->expr) return cur;
   if (is_flonum(ty))
   {
      write_double(buf + offset, eval_double(init->expr), ty->size);
      return cur;
   }
   {
      char **label = NULL;
      long long val = eval2(init->expr, &label);
      if (!label)
      {
         write_buf(buf + offset, (unsigned long long)val, ty->size);
         return cur;
      }
      if (ty->size != 2 && ty->size != 4)
         error_tok(init->expr->tok, "address constant does not fit");
      {
         Relocation *rel = xcalloc(sizeof(Relocation));
         rel->offset = offset + (ty->size == 4 ? 2 : 0);
         rel->label = label;
         rel->addend = (long)val;
         cur->next = rel;
         return cur->next;
      }
   }
}

static void gvar_initializer(Token **rest, Token *tok, Obj *var)
{
   Initializer *init = initializer(rest, tok, var->ty, &var->ty);
   Relocation head;
   char *buf = xcalloc(var->ty->size);
   head.next = NULL;
   write_gvar_data(&head, init, var->ty, buf, 0);
   var->init_data = buf;
   var->rel = head.next;
}

static int is_typename(Token *tok)
{
   static const char *kw[] = {
      "void", "char", "short", "int", "long", "struct", "union", "typedef",
      "float", "double", "signed", "unsigned", "enum", "static", "extern",
      "auto", "register", "const", "volatile", "inline", "__inline",
      "__signed__", "__const", "__volatile__", NULL };
   static HashMap map;
   int i;
   if (map.capacity == 0) for (i = 0; kw[i]; i++) hm_put(&map, kw[i], (void *)1);
   return hm_get2(&map, tok->loc, tok->len) || find_typedef(tok);
}

/* asm-stmt = "asm" "(" string-literal ")" ";" */
static Node *asm_stmt(Token **rest, Token *tok)
{
   Node *node = new_node(ND_ASM, tok);
   tok = tok->next;
   while (equal(tok, "volatile") || equal(tok, "__volatile__")) tok = tok->next;
   tok = skip(tok, "(");
   if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
      error_tok(tok, "expected string literal");
   node->asm_str = tok->str;
   if (current_fn) vec_push(&current_fn->asms, tok->str);
   *rest = skip(skip(tok->next, ")"), ";");
   return node;
}

static int is_asm(Token *tok)
{
   return equal(tok, "asm") || equal(tok, "__asm") || equal(tok, "__asm__");
}

static Node *stmt(Token **rest, Token *tok)
{
   if (equal(tok, "return"))
   {
      Node *node = new_node(ND_RETURN, tok);
      Node *exp;
      Type *ty = current_fn->ty->return_ty;
      if (consume(rest, tok->next, ";"))
      {
         if (ty->kind != TY_VOID) warn_tok(tok, "return without value in function returning non-void");
         return node;
      }
      exp = expr(&tok, tok->next);
      *rest = skip(tok, ";");
      add_type(exp);
      if (ty->kind == TY_VOID)
      {
         warn_tok(exp->tok, "return with a value in function returning void");
         node->lhs = exp;
         return node;
      }
      if (ty->kind != TY_STRUCT && ty->kind != TY_UNION) exp = new_cast(exp, ty);
      node->lhs = exp;
      return node;
   }
   if (equal(tok, "if"))
   {
      Node *node = new_node(ND_IF, tok);
      tok = skip(tok->next, "(");
      node->cond = expr(&tok, tok);
      tok = skip(tok, ")");
      node->then = stmt(&tok, tok);
      if (equal(tok, "else")) node->els = stmt(&tok, tok->next);
      *rest = tok;
      return node;
   }
   if (equal(tok, "switch"))
   {
      Node *node = new_node(ND_SWITCH, tok), *sw;
      char *brk;
      tok = skip(tok->next, "(");
      node->cond = expr(&tok, tok);
      add_type(node->cond);
      if (!is_integer(node->cond->ty)) error_tok(tok, "switch quantity is not an integer");
      if (node->cond->ty->size < 2) node->cond = new_cast(node->cond, ty_int);
      if (node->cond->ty->kind == TY_ENUM) node->cond = new_cast(node->cond, ty_int);
      tok = skip(tok, ")");
      sw = current_switch;
      current_switch = node;
      brk = brk_label;
      brk_label = node->brk_label = new_unique_name();
      node->then = stmt(rest, tok);
      current_switch = sw;
      brk_label = brk;
      return node;
   }
   if (equal(tok, "case"))
   {
      Node *node = new_node(ND_CASE, tok);
      long long val;
      if (!current_switch) error_tok(tok, "stray case");
      val = const_expr(&tok, tok->next);
      tok = skip(tok, ":");
      node->label = new_unique_name();
      node->lhs = stmt(rest, tok);
      if (current_switch->cond->ty->size == 2)
         val = current_switch->cond->ty->is_unsigned ? (unsigned short)val : (short)val;
      else
         val = current_switch->cond->ty->is_unsigned ? (uint32_t)val : (int32_t)val;
      {
         Node *n;
         for (n = current_switch->case_next; n; n = n->case_next)
            if (n->case_val == val) error_tok(node->tok, "duplicate case value");
      }
      node->case_val = (long)val;
      node->case_next = current_switch->case_next;
      current_switch->case_next = node;
      return node;
   }
   if (equal(tok, "default"))
   {
      Node *node = new_node(ND_CASE, tok);
      if (!current_switch) error_tok(tok, "stray default");
      if (current_switch->default_case) error_tok(tok, "multiple default labels");
      tok = skip(tok->next, ":");
      node->label = new_unique_name();
      node->lhs = stmt(rest, tok);
      current_switch->default_case = node;
      return node;
   }
   if (equal(tok, "for"))
   {
      Node *node = new_node(ND_FOR, tok);
      char *brk = brk_label, *cont = cont_label;
      tok = skip(tok->next, "(");
      brk_label = node->brk_label = new_unique_name();
      cont_label = node->cont_label = new_unique_name();
      if (!equal(tok, ";")) node->init = expr_stmt(&tok, tok);
      else tok = tok->next;
      if (!equal(tok, ";")) node->cond = expr(&tok, tok);
      tok = skip(tok, ";");
      if (!equal(tok, ")")) node->inc = expr(&tok, tok);
      tok = skip(tok, ")");
      node->then = stmt(rest, tok);
      brk_label = brk;
      cont_label = cont;
      return node;
   }
   if (equal(tok, "while"))
   {
      Node *node = new_node(ND_FOR, tok);
      char *brk = brk_label, *cont = cont_label;
      tok = skip(tok->next, "(");
      node->cond = expr(&tok, tok);
      tok = skip(tok, ")");
      brk_label = node->brk_label = new_unique_name();
      cont_label = node->cont_label = new_unique_name();
      node->then = stmt(rest, tok);
      brk_label = brk;
      cont_label = cont;
      return node;
   }
   if (equal(tok, "do"))
   {
      Node *node = new_node(ND_DO, tok);
      char *brk = brk_label, *cont = cont_label;
      brk_label = node->brk_label = new_unique_name();
      cont_label = node->cont_label = new_unique_name();
      node->then = stmt(&tok, tok->next);
      brk_label = brk;
      cont_label = cont;
      tok = skip(tok, "while");
      tok = skip(tok, "(");
      node->cond = expr(&tok, tok);
      tok = skip(tok, ")");
      *rest = skip(tok, ";");
      return node;
   }
   if (is_asm(tok)) return asm_stmt(rest, tok);
   if (equal(tok, "goto"))
   {
      Node *node = new_node(ND_GOTO, tok);
      node->label = get_ident(tok->next);
      node->goto_next = gotos;
      gotos = node;
      *rest = skip(tok->next->next, ";");
      return node;
   }
   if (equal(tok, "break"))
   {
      Node *node;
      if (!brk_label) error_tok(tok, "stray break");
      node = new_node(ND_GOTO, tok);
      node->unique_label = brk_label;
      *rest = skip(tok->next, ";");
      return node;
   }
   if (equal(tok, "continue"))
   {
      Node *node;
      if (!cont_label) error_tok(tok, "stray continue");
      node = new_node(ND_GOTO, tok);
      node->unique_label = cont_label;
      *rest = skip(tok->next, ";");
      return node;
   }
   if (tok->kind == TK_IDENT && equal(tok->next, ":"))
   {
      Node *node = new_node(ND_LABEL, tok);
      node->label = xstrndup(tok->loc, tok->len);
      node->unique_label = new_unique_name();
      node->lhs = stmt(rest, tok->next->next);
      node->goto_next = labels;
      labels = node;
      return node;
   }
   if (equal(tok, "{")) return compound_stmt(rest, tok->next);
   return expr_stmt(rest, tok);
}

static Node *compound_stmt(Token **rest, Token *tok)
{
   Node *node = new_node(ND_BLOCK, tok);
   Node head, *cur = &head;
   head.next = NULL;
   enter_scope();
   while (!equal(tok, "}"))
   {
      if (tok->kind == TK_EOF) error_tok(tok, "missing '}'");
      if (is_typename(tok) && !equal(tok->next, ":"))
      {
         VarAttr attr;
         Type *basety;
         memset(&attr, 0, sizeof(attr));
         basety = declspec(&tok, tok, &attr);
         if (attr.is_typedef) { tok = parse_typedef(tok, basety); continue; }
         cur = cur->next = declaration(&tok, tok, basety, &attr);
      }
      else cur = cur->next = stmt(&tok, tok);
      add_type(cur);
   }
   leave_scope();
   node->body = head.next;
   *rest = tok->next;
   return node;
}

static Node *expr_stmt(Token **rest, Token *tok)
{
   Node *node;
   if (equal(tok, ";"))
   {
      *rest = tok->next;
      return new_node(ND_BLOCK, tok);
   }
   node = new_node(ND_EXPR_STMT, tok);
   node->lhs = expr(&tok, tok);
   *rest = skip(tok, ";");
   return node;
}

static Node *expr(Token **rest, Token *tok)
{
   Node *node = assign(&tok, tok);
   if (equal(tok, ","))
      return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);
   *rest = tok;
   return node;
}

/* ---------------- constant evaluation ---------------- */

static long long wrap(long long v, Type *ty)
{
   if (ty->kind == TY_PTR) return (unsigned short)v;
   if (!is_integer(ty)) return v;
   switch (ty->size)
   {
      case 1: return ty->is_unsigned ? (unsigned char)v : (signed char)v;
      case 2: return ty->is_unsigned ? (unsigned short)v : (short)v;
      case 4: return ty->is_unsigned ? (uint32_t)v : (int32_t)v;
   }
   return v;
}

static long long eval(Node *node) { return eval2(node, NULL); }

static long long eval2(Node *node, char ***label)
{
   long long r;
   add_type(node);
   if (is_flonum(node->ty)) return (long long)eval_double(node);
   switch (node->kind)
   {
      case ND_ADD: r = eval2(node->lhs, label) + eval(node->rhs); break;
      case ND_SUB: r = eval2(node->lhs, label) - eval(node->rhs); break;
      case ND_MUL: r = eval(node->lhs) * eval(node->rhs); break;
      case ND_DIV:
      {
         long long d = eval(node->rhs);
         if (d == 0) error_tok(node->tok, "division by zero");
         if (node->ty->is_unsigned)
            r = (long long)((unsigned long long)wrap(eval(node->lhs), node->ty) /
                            (unsigned long long)wrap(d, node->ty));
         else r = eval(node->lhs) / d;
         break;
      }
      case ND_NEG: r = -eval(node->lhs); break;
      case ND_MOD:
      {
         long long d = eval(node->rhs);
         if (d == 0) error_tok(node->tok, "division by zero");
         if (node->ty->is_unsigned)
            r = (long long)((unsigned long long)wrap(eval(node->lhs), node->ty) %
                            (unsigned long long)wrap(d, node->ty));
         else r = eval(node->lhs) % d;
         break;
      }
      case ND_BITAND: r = eval(node->lhs) & eval(node->rhs); break;
      case ND_BITOR:  r = eval(node->lhs) | eval(node->rhs); break;
      case ND_BITXOR: r = eval(node->lhs) ^ eval(node->rhs); break;
      case ND_SHL:    r = eval(node->lhs) << eval(node->rhs); break;
      case ND_SHR:
         if (node->ty->is_unsigned)
            r = (long long)((unsigned long long)wrap(eval(node->lhs), node->ty) >> eval(node->rhs));
         else r = eval(node->lhs) >> eval(node->rhs);
         break;
      case ND_EQ: r = eval(node->lhs) == eval(node->rhs); break;
      case ND_NE: r = eval(node->lhs) != eval(node->rhs); break;
      case ND_LT:
         if (node->lhs->ty->is_unsigned)
            r = (unsigned long long)eval(node->lhs) < (unsigned long long)eval(node->rhs);
         else r = eval(node->lhs) < eval(node->rhs);
         break;
      case ND_LE:
         if (node->lhs->ty->is_unsigned)
            r = (unsigned long long)eval(node->lhs) <= (unsigned long long)eval(node->rhs);
         else r = eval(node->lhs) <= eval(node->rhs);
         break;
      case ND_COND: return eval(node->cond) ? eval2(node->then, label) : eval2(node->els, label);
      case ND_COMMA: return eval2(node->rhs, label);
      case ND_NOT: return !eval(node->lhs);
      case ND_BITNOT: r = ~eval(node->lhs); break;
      case ND_LOGAND: return eval(node->lhs) && eval(node->rhs);
      case ND_LOGOR: return eval(node->lhs) || eval(node->rhs);
      case ND_CAST:
      {
         long long val;
         if (is_flonum(node->lhs->ty))
         {
            double d = eval_double(node->lhs);
            if (node->ty->is_unsigned && d >= 0) return wrap((long long)(unsigned long long)d, node->ty);
            return wrap((long long)d, node->ty);
         }
         val = eval2(node->lhs, label);
         if (is_integer(node->ty) || node->ty->kind == TY_PTR) return wrap(val, node->ty);
         return val;
      }
      case ND_ADDR: return eval_rval(node->lhs, label);
      case ND_DEREF:
         if (node->ty->kind == TY_ARRAY && label) return eval2(node->lhs, label);
         error_tok(node->tok, "not a compile-time constant");
         return 0;
      case ND_MEMBER:
         if (!label) error_tok(node->tok, "not a compile-time constant");
         if (node->ty->kind != TY_ARRAY) error_tok(node->tok, "invalid initializer");
         return eval_rval(node->lhs, label) + node->member->offset;
      case ND_VAR:
         if (!label) error_tok(node->tok, "not a compile-time constant");
         if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
            error_tok(node->tok, "invalid initializer");
         if (node->var->is_local) error_tok(node->tok, "not a compile-time constant");
         *label = &node->var->asmname;
         node->var->is_used = 1;
         return 0;
      case ND_NUM: return node->val;
      default:
         error_tok(node->tok, "not a compile-time constant");
         return 0;
   }
   return wrap(r, node->ty);
}

static long long eval_rval(Node *node, char ***label)
{
   switch (node->kind)
   {
      case ND_VAR:
         if (node->var->is_local) error_tok(node->tok, "not a compile-time constant");
         *label = &node->var->asmname;
         return 0;
      case ND_DEREF: return eval2(node->lhs, label);
      case ND_MEMBER: return eval_rval(node->lhs, label) + node->member->offset;
      default: break;
   }
   error_tok(node->tok, "invalid initializer");
   return 0;
}

static int is_const_expr(Node *node)
{
   add_type(node);
   switch (node->kind)
   {
      case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_BITAND:
      case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR: case ND_EQ:
      case ND_NE: case ND_LT: case ND_LE: case ND_LOGAND: case ND_LOGOR: case ND_MOD:
         return is_const_expr(node->lhs) && is_const_expr(node->rhs);
      case ND_COND:
         if (!is_const_expr(node->cond)) return 0;
         return is_const_expr(eval(node->cond) ? node->then : node->els);
      case ND_COMMA: return is_const_expr(node->rhs);
      case ND_NEG: case ND_NOT: case ND_BITNOT: case ND_CAST:
         return is_const_expr(node->lhs);
      case ND_NUM: return 1;
      default: return 0;
   }
}

long long const_expr(Token **rest, Token *tok)
{
   Node *node = conditional(rest, tok);
   return eval(node);
}

static double eval_double(Node *node)
{
   add_type(node);
   if (is_integer(node->ty))
   {
      if (node->ty->is_unsigned) return (double)(unsigned long long)eval(node);
      return (double)eval(node);
   }
   switch (node->kind)
   {
      case ND_ADD: return eval_double(node->lhs) + eval_double(node->rhs);
      case ND_SUB: return eval_double(node->lhs) - eval_double(node->rhs);
      case ND_MUL: return eval_double(node->lhs) * eval_double(node->rhs);
      case ND_DIV: return eval_double(node->lhs) / eval_double(node->rhs);
      case ND_NEG: return -eval_double(node->lhs);
      case ND_COND: return eval_double(node->cond) ? eval_double(node->then) : eval_double(node->els);
      case ND_COMMA: return eval_double(node->rhs);
      case ND_CAST:
      {
         double d;
         if (is_flonum(node->lhs->ty)) d = eval_double(node->lhs);
         else d = eval_double(node->lhs);
         if (node->ty->kind == TY_FLOAT) d = (float)d;
         return d;
      }
      case ND_NUM: return node->fval;
      default: break;
   }
   error_tok(node->tok, "not a compile-time constant");
   return 0;
}

/* ---------------- assignment and compound assignment ---------------- */

static int is_simple_var(Node *n)
{
   return n->kind == ND_VAR;
}

static Obj *new_temp(Type *ty)
{
   char *name = format("$tmp%d", static_count++);
   Obj *var = new_lvar(name, ty);
   return var;
}

/* lower  A op= B  */
static Node *to_assign(Node *binary)
{
   Node *lhs, *rhs;
   Token *tok = binary->tok;
   add_type(binary->lhs);
   add_type(binary->rhs);
   lhs = binary->lhs;

   if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
   {
      /* tmp = &s;  tmp->f = tmp->f op B */
      Obj *var = new_temp(pointer_to(lhs->lhs->ty));
      Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                               new_unary(ND_ADDR, lhs->lhs, tok), tok);
      Node *expr2 = new_unary(ND_MEMBER, new_unary(ND_DEREF, new_var_node(var, tok), tok), tok);
      Node *expr3 = new_unary(ND_MEMBER, new_unary(ND_DEREF, new_var_node(var, tok), tok), tok);
      Node *expr4;
      expr2->member = lhs->member;
      expr3->member = lhs->member;
      binary->lhs = expr3;
      if (binary->kind == ND_ADD) binary = new_add(binary->lhs, binary->rhs, tok);
      else if (binary->kind == ND_SUB) binary = new_sub(binary->lhs, binary->rhs, tok);
      expr4 = new_binary(ND_ASSIGN, expr2, binary, tok);
      return new_binary(ND_COMMA, expr1, expr4, tok);
   }
   if (is_simple_var(lhs))
   {
      Node *copy = new_var_node(lhs->var, tok);
      binary->lhs = copy;
      if (binary->kind == ND_ADD) rhs = new_add(binary->lhs, binary->rhs, tok);
      else if (binary->kind == ND_SUB) rhs = new_sub(binary->lhs, binary->rhs, tok);
      else rhs = binary;
      return new_binary(ND_ASSIGN, lhs, rhs, tok);
   }
   {
      /* tmp = &A, *tmp = *tmp op B */
      Obj *var = new_temp(pointer_to(lhs->ty));
      Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                               new_unary(ND_ADDR, lhs, tok), tok);
      Node *expr2;
      binary->lhs = new_unary(ND_DEREF, new_var_node(var, tok), tok);
      if (binary->kind == ND_ADD) rhs = new_add(binary->lhs, binary->rhs, tok);
      else if (binary->kind == ND_SUB) rhs = new_sub(binary->lhs, binary->rhs, tok);
      else rhs = binary;
      expr2 = new_binary(ND_ASSIGN, new_unary(ND_DEREF, new_var_node(var, tok), tok), rhs, tok);
      return new_binary(ND_COMMA, expr1, expr2, tok);
   }
}

static Node *assign(Token **rest, Token *tok)
{
   Node *node = conditional(&tok, tok);
   static const char *ops[] = { "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=" };
   static const NodeKind kinds[] = { ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD, ND_BITAND,
                                     ND_BITOR, ND_BITXOR, ND_SHL, ND_SHR };
   int i;
   if (equal(tok, "="))
   {
      Node *rhs = assign(rest, tok->next);
      add_type(node);
      add_type(rhs);
      if (node->ty->is_const && node->kind == ND_VAR)
         warn_tok(tok, "assignment of read-only variable");
      return new_binary(ND_ASSIGN, node, rhs, tok);
   }
   for (i = 0; i < 10; i++)
   {
      if (equal(tok, ops[i]))
      {
         Node *rhs = assign(rest, tok->next);
         return to_assign(new_binary(kinds[i], node, rhs, tok));
      }
   }
   *rest = tok;
   return node;
}

static Node *conditional(Token **rest, Token *tok)
{
   Node *cond = logor(&tok, tok), *node;
   if (!equal(tok, "?")) { *rest = tok; return cond; }
   node = new_node(ND_COND, tok);
   node->cond = cond;
   node->then = expr(&tok, tok->next);
   tok = skip(tok, ":");
   node->els = conditional(rest, tok);
   return node;
}

static Node *logor(Token **rest, Token *tok)
{
   Node *node = logand(&tok, tok);
   while (equal(tok, "||"))
   {
      Token *start = tok;
      node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
   }
   *rest = tok;
   return node;
}

static Node *logand(Token **rest, Token *tok)
{
   Node *node = bitor_(&tok, tok);
   while (equal(tok, "&&"))
   {
      Token *start = tok;
      node = new_binary(ND_LOGAND, node, bitor_(&tok, tok->next), start);
   }
   *rest = tok;
   return node;
}

static Node *bitor_(Token **rest, Token *tok)
{
   Node *node = bitxor_(&tok, tok);
   while (equal(tok, "|"))
   {
      Token *start = tok;
      node = new_binary(ND_BITOR, node, bitxor_(&tok, tok->next), start);
   }
   *rest = tok;
   return node;
}

static Node *bitxor_(Token **rest, Token *tok)
{
   Node *node = bitand_(&tok, tok);
   while (equal(tok, "^"))
   {
      Token *start = tok;
      node = new_binary(ND_BITXOR, node, bitand_(&tok, tok->next), start);
   }
   *rest = tok;
   return node;
}

static Node *bitand_(Token **rest, Token *tok)
{
   Node *node = equality(&tok, tok);
   while (equal(tok, "&"))
   {
      Token *start = tok;
      node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
   }
   *rest = tok;
   return node;
}

static Node *equality(Token **rest, Token *tok)
{
   Node *node = relational(&tok, tok);
   for (;;)
   {
      Token *start = tok;
      if (equal(tok, "=="))
      { node = new_binary(ND_EQ, node, relational(&tok, tok->next), start); continue; }
      if (equal(tok, "!="))
      { node = new_binary(ND_NE, node, relational(&tok, tok->next), start); continue; }
      *rest = tok;
      return node;
   }
}

static Node *relational(Token **rest, Token *tok)
{
   Node *node = shift(&tok, tok);
   for (;;)
   {
      Token *start = tok;
      if (equal(tok, "<"))
      { node = new_binary(ND_LT, node, shift(&tok, tok->next), start); continue; }
      if (equal(tok, "<="))
      { node = new_binary(ND_LE, node, shift(&tok, tok->next), start); continue; }
      if (equal(tok, ">"))
      { node = new_binary(ND_LT, shift(&tok, tok->next), node, start); continue; }
      if (equal(tok, ">="))
      { node = new_binary(ND_LE, shift(&tok, tok->next), node, start); continue; }
      *rest = tok;
      return node;
   }
}

static Node *shift(Token **rest, Token *tok)
{
   Node *node = add(&tok, tok);
   for (;;)
   {
      Token *start = tok;
      if (equal(tok, "<<"))
      { node = new_binary(ND_SHL, node, add(&tok, tok->next), start); continue; }
      if (equal(tok, ">>"))
      { node = new_binary(ND_SHR, node, add(&tok, tok->next), start); continue; }
      *rest = tok;
      return node;
   }
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok)
{
   add_type(lhs);
   add_type(rhs);
   if (is_numeric(lhs->ty) && is_numeric(rhs->ty)) return new_binary(ND_ADD, lhs, rhs, tok);
   if (lhs->ty->base && rhs->ty->base) error_tok(tok, "invalid operands");
   if (!lhs->ty->base && rhs->ty->base) { Node *t = lhs; lhs = rhs; rhs = t; }
   if (!lhs->ty->base || !is_integer(rhs->ty)) error_tok(tok, "invalid operands");
   if (lhs->ty->base->kind == TY_VOID) warn_tok(tok, "pointer of type void * used in arithmetic");
   if (lhs->ty->base->size == 0 && lhs->ty->base->kind != TY_VOID)
      error_tok(tok, "arithmetic on pointer to incomplete type");
   /* ptr + int: scale, index arithmetic is 16 bit */
   rhs = new_cast(rhs, ty_int);
   if (lhs->ty->base->size > 1)
      rhs = new_binary(ND_MUL, rhs, new_num(lhs->ty->base->size, tok), tok);
   {
      Node *n = new_binary(ND_ADD, lhs, rhs, tok);
      add_type(n->lhs);
      add_type(n->rhs);
      n->ty = lhs->ty->kind == TY_ARRAY ? pointer_to(lhs->ty->base) : lhs->ty;
      return n;
   }
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok)
{
   add_type(lhs);
   add_type(rhs);
   if (is_numeric(lhs->ty) && is_numeric(rhs->ty)) return new_binary(ND_SUB, lhs, rhs, tok);
   if (lhs->ty->base && is_integer(rhs->ty))
   {
      Node *n;
      rhs = new_cast(rhs, ty_int);
      if (lhs->ty->base->size > 1)
         rhs = new_binary(ND_MUL, rhs, new_num(lhs->ty->base->size, tok), tok);
      add_type(rhs);
      n = new_binary(ND_SUB, lhs, rhs, tok);
      n->ty = lhs->ty->kind == TY_ARRAY ? pointer_to(lhs->ty->base) : lhs->ty;
      return n;
   }
   if (lhs->ty->base && rhs->ty->base)
   {
      /* ptr - ptr: number of elements, result int (ptrdiff_t) */
      Node *n = new_binary(ND_SUB, new_cast(lhs, ty_int), new_cast(rhs, ty_int), tok);
      n->ty = ty_int;
      if (lhs->ty->base->size > 1)
         return new_binary(ND_DIV, n, new_num(lhs->ty->base->size, tok), tok);
      return n;
   }
   error_tok(tok, "invalid operands");
   return NULL;
}

static Node *add(Token **rest, Token *tok)
{
   Node *node = mul(&tok, tok);
   for (;;)
   {
      Token *start = tok;
      if (equal(tok, "+")) { node = new_add(node, mul(&tok, tok->next), start); continue; }
      if (equal(tok, "-")) { node = new_sub(node, mul(&tok, tok->next), start); continue; }
      *rest = tok;
      return node;
   }
}

static Node *mul(Token **rest, Token *tok)
{
   Node *node = cast(&tok, tok);
   for (;;)
   {
      Token *start = tok;
      if (equal(tok, "*")) { node = new_binary(ND_MUL, node, cast(&tok, tok->next), start); continue; }
      if (equal(tok, "/")) { node = new_binary(ND_DIV, node, cast(&tok, tok->next), start); continue; }
      if (equal(tok, "%")) { node = new_binary(ND_MOD, node, cast(&tok, tok->next), start); continue; }
      *rest = tok;
      return node;
   }
}

static Node *cast(Token **rest, Token *tok)
{
   if (equal(tok, "(") && is_typename(tok->next))
   {
      Token *start = tok;
      Type *ty = typename_(&tok, tok->next);
      Node *node;
      tok = skip(tok, ")");
      node = new_cast(cast(rest, tok), ty);
      node->tok = start;
      return node;
   }
   return unary(rest, tok);
}

/* post increment / decrement */
static Node *new_inc_dec(Node *node, Token *tok, int addend)
{
   add_type(node);
   if (is_flonum(node->ty) || (node->kind == ND_MEMBER && node->member->is_bitfield))
   {
      /* (tmp = &x, old = *tmp, *tmp = old + 1, old) */
      Node *e;
      Obj *old = new_temp(node->ty);
      if (node->kind == ND_VAR)
      {
         e = new_binary(ND_ASSIGN, new_var_node(old, tok), node, tok);
         e = new_binary(ND_COMMA, e,
               new_binary(ND_ASSIGN, new_var_node(node->var, tok),
                  new_add(new_var_node(old, tok), new_num(addend, tok), tok), tok), tok);
         return new_binary(ND_COMMA, e, new_var_node(old, tok), tok);
      }
      if (node->kind == ND_MEMBER)
      {
         Obj *p = new_temp(pointer_to(node->lhs->ty));
         Node *m1, *m2;
         e = new_binary(ND_ASSIGN, new_var_node(p, tok), new_unary(ND_ADDR, node->lhs, tok), tok);
         m1 = new_unary(ND_MEMBER, new_unary(ND_DEREF, new_var_node(p, tok), tok), tok);
         m1->member = node->member;
         m2 = new_unary(ND_MEMBER, new_unary(ND_DEREF, new_var_node(p, tok), tok), tok);
         m2->member = node->member;
         e = new_binary(ND_COMMA, e, new_binary(ND_ASSIGN, new_var_node(old, tok), m1, tok), tok);
         e = new_binary(ND_COMMA, e, new_binary(ND_ASSIGN, m2,
                 new_add(new_var_node(old, tok), new_num(addend, tok), tok), tok), tok);
         return new_binary(ND_COMMA, e, new_var_node(old, tok), tok);
      }
      {
         Obj *p = new_temp(pointer_to(node->ty));
         e = new_binary(ND_ASSIGN, new_var_node(p, tok), new_unary(ND_ADDR, node, tok), tok);
         e = new_binary(ND_COMMA, e, new_binary(ND_ASSIGN, new_var_node(old, tok),
                 new_unary(ND_DEREF, new_var_node(p, tok), tok), tok), tok);
         e = new_binary(ND_COMMA, e, new_binary(ND_ASSIGN,
                 new_unary(ND_DEREF, new_var_node(p, tok), tok),
                 new_add(new_var_node(old, tok), new_num(addend, tok), tok), tok), tok);
         return new_binary(ND_COMMA, e, new_var_node(old, tok), tok);
      }
   }
   return new_cast(new_add(to_assign(new_binary(ND_ADD, node, new_num(addend, tok), tok)),
                           new_num(-addend, tok), tok), node->ty);
}

static Node *unary(Token **rest, Token *tok)
{
   if (equal(tok, "+"))
   {
      Node *n = cast(rest, tok->next);
      add_type(n);
      if (is_integer(n->ty)) return new_cast(n, get_common_type_pub(n->ty));
      return n;
   }
   if (equal(tok, "-")) return new_unary(ND_NEG, cast(rest, tok->next), tok);
   if (equal(tok, "&"))
   {
      Node *lhs = cast(rest, tok->next);
      add_type(lhs);
      if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
         error_tok(tok, "cannot take address of bitfield");
      return new_unary(ND_ADDR, lhs, tok);
   }
   if (equal(tok, "*"))
   {
      Node *node = cast(rest, tok->next);
      add_type(node);
      if (node->ty->kind == TY_FUNC) return node;
      return new_unary(ND_DEREF, node, tok);
   }
   if (equal(tok, "!")) return new_unary(ND_NOT, cast(rest, tok->next), tok);
   if (equal(tok, "~")) return new_unary(ND_BITNOT, cast(rest, tok->next), tok);
   if (equal(tok, "++"))
      return to_assign(new_binary(ND_ADD, unary(rest, tok->next), new_num(1, tok), tok));
   if (equal(tok, "--"))
      return to_assign(new_binary(ND_SUB, unary(rest, tok->next), new_num(1, tok), tok));
   return postfix(rest, tok);
}

static void struct_members(Token **rest, Token *tok, Type *ty)
{
   Member head, *cur = &head;
   int idx = 0;
   head.next = NULL;
   while (!equal(tok, "}"))
   {
      VarAttr attr;
      Type *basety;
      int first = 1;
      memset(&attr, 0, sizeof(attr));
      basety = declspec(&tok, tok, &attr);
      while (!consume(&tok, tok, ";"))
      {
         Member *mem;
         if (!first) tok = skip(tok, ",");
         first = 0;
         mem = xcalloc(sizeof(Member));
         if (equal(tok, ":"))
         {
            mem->ty = basety;   /* unnamed bitfield */
         }
         else
         {
            mem->ty = declarator(&tok, tok, basety);
            mem->name = mem->ty->name;
         }
         mem->idx = idx++;
         if (consume(&tok, tok, ":"))
         {
            mem->is_bitfield = 1;
            mem->bit_width = (int)const_expr(&tok, tok);
            if (!is_integer(mem->ty)) error_tok(tok, "bit-field has invalid type");
            if (mem->bit_width < 0 || mem->bit_width > 16)
               error_tok(tok, "invalid bit-field width");
         }
         cur = cur->next = mem;
      }
   }
   if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0)
   {
      cur->ty = array_of(cur->ty->base, 0);
      ty->is_flexible = 1;
   }
   *rest = tok->next;
   ty->members = head.next;
}

static Type *struct_union_decl(Token **rest, Token *tok)
{
   Token *tag = NULL;
   Type *ty = struct_type();
   if (tok->kind == TK_IDENT) { tag = tok; tok = tok->next; }
   if (tag && !equal(tok, "{"))
   {
      Type *ty2;
      *rest = tok;
      ty2 = find_tag(tag);
      if (ty2) return ty2;
      ty->size = -1;
      ty->is_incomplete = 1;
      push_tag_scope(tag, ty);
      return ty;
   }
   tok = skip(tok, "{");
   struct_members(rest, tok, ty);
   ty->align = 1;
   if (tag)
   {
      Type *ty2 = hm_get2(&scope->tags, tag->loc, tag->len);
      if (ty2 && ty2->is_incomplete)
      {
         /* complete the earlier declaration in place */
         *ty2 = *ty;
         return ty2;
      }
      push_tag_scope(tag, ty);
   }
   return ty;
}

static Type *struct_decl(Token **rest, Token *tok)
{
   Type *ty = struct_union_decl(rest, tok);
   Member *mem;
   int offset = 0, bits_used = 16;   /* 16: no open unit */
   int unit_offset = 0;
   ty->kind = TY_STRUCT;
   if (ty->size < 0 && ty->is_incomplete && !ty->members) return ty;
   if (ty->size > 0 || (ty->members == NULL && !ty->is_incomplete && ty->size == 0 && 0))
      return ty;
   for (mem = ty->members; mem; mem = mem->next)
   {
      if (mem->is_bitfield)
      {
         if (mem->bit_width == 0) { bits_used = 16; continue; }
         if (bits_used + mem->bit_width > 16)
         {
            unit_offset = offset;
            offset += 2;
            bits_used = 0;
         }
         mem->offset = unit_offset;
         /* allocate from the most significant bit downwards */
         mem->bit_offset = 16 - bits_used - mem->bit_width;
         bits_used += mem->bit_width;
         continue;
      }
      bits_used = 16;
      if (mem->ty->size < 0 || (mem->ty->kind == TY_STRUCT && mem->ty->is_incomplete))
         error_tok(mem->name ? mem->name : tok, "field has incomplete type");
      mem->offset = offset;
      offset += mem->ty->size;
   }
   ty->size = offset;
   ty->is_incomplete = 0;
   return ty;
}

static Type *union_decl(Token **rest, Token *tok)
{
   Type *ty = struct_union_decl(rest, tok);
   Member *mem;
   ty->kind = TY_UNION;
   if (ty->size < 0 && ty->is_incomplete && !ty->members) return ty;
   if (ty->size > 0) return ty;
   ty->size = 0;
   for (mem = ty->members; mem; mem = mem->next)
   {
      mem->offset = 0;
      if (mem->is_bitfield)
      {
         mem->bit_offset = 16 - mem->bit_width;
         if (ty->size < 2) ty->size = 2;
         continue;
      }
      if (ty->size < mem->ty->size) ty->size = mem->ty->size;
   }
   ty->is_incomplete = 0;
   return ty;
}

static Member *get_struct_member(Type *ty, Token *tok)
{
   Member *mem;
   for (mem = ty->members; mem; mem = mem->next)
      if (mem->name && mem->name->len == tok->len && !strncmp(mem->name->loc, tok->loc, tok->len))
         return mem;
   return NULL;
}

static Node *struct_ref(Node *node, Token *tok)
{
   Member *mem;
   Node *n;
   add_type(node);
   if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION)
      error_tok(node->tok, "not a struct nor a union");
   if (node->ty->is_incomplete) error_tok(tok, "dereferencing incomplete type");
   mem = get_struct_member(node->ty, tok);
   if (!mem) error_tok(tok, "no such member");
   n = new_unary(ND_MEMBER, node, tok);
   n->member = mem;
   return n;
}

static Node *postfix(Token **rest, Token *tok)
{
   Node *node = primary(&tok, tok);
   for (;;)
   {
      if (equal(tok, "("))
      {
         node = funcall(&tok, tok->next, node);
         continue;
      }
      if (equal(tok, "["))
      {
         Token *start = tok;
         Node *idx = expr(&tok, tok->next);
         tok = skip(tok, "]");
         node = new_unary(ND_DEREF, new_add(node, idx, start), start);
         continue;
      }
      if (equal(tok, "."))
      {
         node = struct_ref(node, tok->next);
         tok = tok->next->next;
         continue;
      }
      if (equal(tok, "->"))
      {
         node = new_unary(ND_DEREF, node, tok);
         node = struct_ref(node, tok->next);
         tok = tok->next->next;
         continue;
      }
      if (equal(tok, "++")) { node = new_inc_dec(node, tok, 1); tok = tok->next; continue; }
      if (equal(tok, "--")) { node = new_inc_dec(node, tok, -1); tok = tok->next; continue; }
      *rest = tok;
      return node;
   }
}

static Type *default_promote(Type *ty)
{
   if (is_integer(ty) && ty->size < 2) return ty_int;
   if (ty->kind == TY_SHORT) return ty->is_unsigned ? ty_uint : ty_int;
   if (ty->kind == TY_ENUM) return ty_int;
   if (ty->kind == TY_FLOAT) return ty_double;
   return ty;
}

static Node *funcall(Token **rest, Token *tok, Node *fn)
{
   Type *ty, *param_ty;
   Node head, *cur = &head, *node;
   head.next = NULL;
   add_type(fn);
   if (fn->ty->kind != TY_FUNC &&
       (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
      error_tok(fn->tok, "not a function");
   ty = fn->ty->kind == TY_FUNC ? fn->ty : fn->ty->base;
   param_ty = ty->has_proto ? ty->params : NULL;

   while (!equal(tok, ")"))
   {
      Node *arg;
      if (cur != &head) tok = skip(tok, ",");
      arg = assign(&tok, tok);
      add_type(arg);
      if (!param_ty && ty->has_proto && !ty->is_variadic) error_tok(tok, "too many arguments");
      if (param_ty)
      {
         if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
            arg = new_cast(arg, param_ty);
         param_ty = param_ty->next;
      }
      else
      {
         if (arg->ty->kind == TY_ARRAY) arg = new_cast(arg, pointer_to(arg->ty->base));
         else if (arg->ty->kind == TY_FUNC) arg = new_cast(arg, pointer_to(arg->ty));
         else if (arg->ty->kind != TY_STRUCT && arg->ty->kind != TY_UNION)
            arg = new_cast(arg, default_promote(arg->ty));
      }
      cur = cur->next = arg;
   }
   if (param_ty) error_tok(tok, "too few arguments");
   *rest = skip(tok, ")");
   node = new_unary(ND_FUNCALL, fn, tok);
   node->func_ty = ty;
   node->ty = ty->return_ty;
   node->args = head.next;
   if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
      node->ret_buffer = new_lvar(format("$ret%d", static_count++), node->ty);
   return node;
}

static Node *primary(Token **rest, Token *tok)
{
   Token *start = tok;
   if (equal(tok, "("))
   {
      Node *node = expr(&tok, tok->next);
      *rest = skip(tok, ")");
      return node;
   }
   if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next))
   {
      Type *ty = typename_(&tok, tok->next->next);
      *rest = skip(tok, ")");
      if (ty->size < 0 || (ty->kind == TY_STRUCT && ty->is_incomplete))
         error_tok(start, "sizeof of incomplete type");
      if (ty->kind == TY_FUNC) error_tok(start, "sizeof of a function type");
      return new_uint(ty->size, start);
   }
   if (equal(tok, "sizeof"))
   {
      Node *node = unary(rest, tok->next);
      add_type(node);
      if (node->ty->kind == TY_FUNC) error_tok(start, "sizeof of a function");
      if (node->ty->kind == TY_STRUCT && node->ty->is_incomplete)
         error_tok(start, "sizeof of incomplete type");
      return new_uint(node->ty->size, tok);
   }
   if (tok->kind == TK_IDENT)
   {
      VarScope *sc = find_var(tok);
      *rest = tok->next;
      if (sc)
      {
         if (sc->var)
         {
            return new_var_node(sc->var, tok);
         }
         if (sc->enum_ty) return new_num(sc->enum_val, tok);
      }
      if (equal(tok->next, "("))
      {
         /* implicit declaration: extern int name(); */
         Obj *g;
         Type *fty = func_type(ty_int);
         Scope *sc0 = scope;
         VarScope *vs;
         warn_tok(tok, "implicit declaration of function '%.*s'", tok->len, tok->loc);
         g = xcalloc(sizeof(Obj));
         g->name = xstrndup(tok->loc, tok->len);
         g->asmname = format("_%s", g->name);
         g->ty = fty;
         g->is_function = 1;
         g->is_extern = 1;
         g->next = globals;
         globals = g;
         while (sc0->next) sc0 = sc0->next;
         vs = xcalloc(sizeof(VarScope));
         vs->var = g;
         hm_put(&sc0->vars, g->name, vs);
         return new_var_node(g, tok);
      }
      error_tok(tok, "undeclared identifier '%.*s'", tok->len, tok->loc);
   }
   if (tok->kind == TK_STR)
   {
      Obj *var = new_string_literal(tok->str, tok->strlen, tok->ty);
      *rest = tok->next;
      return new_var_node(var, tok);
   }
   if (tok->kind == TK_NUM)
   {
      Node *node;
      if (is_flonum(tok->ty))
      {
         node = new_node(ND_NUM, tok);
         node->fval = tok->fval;
      }
      else node = new_num(tok->val, tok);
      node->ty = tok->ty;
      *rest = tok->next;
      return node;
   }
   error_tok(tok, "expected an expression");
   return NULL;
}

static Token *parse_typedef(Token *tok, Type *basety)
{
   int first = 1;
   while (!consume(&tok, tok, ";"))
   {
      Type *ty;
      if (!first) tok = skip(tok, ",");
      first = 0;
      ty = declarator(&tok, tok, basety);
      if (!ty->name) error_tok(tok, "typedef name omitted");
      push_scope(get_ident(ty->name))->type_def = ty;
   }
   return tok;
}

static Obj *find_func_global(char *name)
{
   Obj *o;
   for (o = globals; o; o = o->next) if (!strcmp(o->name, name)) return o;
   return NULL;
}

static void create_param_lvars(Type *param)
{
   if (param)
   {
      Obj *v;
      create_param_lvars(param->next);
      if (!param->name) error_tok(param->name ? param->name : current_fn->tok, "parameter name omitted");
      v = new_lvar(get_ident(param->name), param);
      v->is_param = 1;
      v->tok = param->name;
   }
}

static void resolve_goto_labels(void)
{
   Node *x, *y;
   for (x = gotos; x; x = x->goto_next)
   {
      for (y = labels; y; y = y->goto_next)
         if (!strcmp(x->label, y->label)) { x->unique_label = y->unique_label; break; }
      if (x->unique_label == NULL) error_tok(x->tok->next, "use of undeclared label");
   }
   gotos = labels = NULL;
}

static Token *function(Token *tok, Type *basety, VarAttr *attr)
{
   Type *ty = declarator(&tok, tok, basety);
   char *name;
   Obj *fn;
   if (!ty->name) error_tok(tok, "function name omitted");
   name = get_ident(ty->name);

   /* old style parameter declarations */
   if (ty->is_variadic == 2)
   {
      Type *p;
      ty->is_variadic = 0;
      while (!equal(tok, "{") && !equal(tok, ";"))
      {
         VarAttr a2;
         Type *bt;
         int first = 1;
         memset(&a2, 0, sizeof(a2));
         bt = declspec(&tok, tok, &a2);
         while (!consume(&tok, tok, ";"))
         {
            Type *pt;
            int found = 0;
            if (!first) tok = skip(tok, ",");
            first = 0;
            pt = declarator(&tok, tok, bt);
            if (pt->kind == TY_ARRAY) { Token *n = pt->name; pt = pointer_to(pt->base); pt->name = n; }
            else if (pt->kind == TY_FUNC) { Token *n = pt->name; pt = pointer_to(pt); pt->name = n; }
            for (p = ty->params; p; p = p->next)
            {
               if (p->name->len == pt->name->len && !strncmp(p->name->loc, pt->name->loc, p->name->len))
               {
                  Type *nx = p->next;
                  Token *nm = p->name;
                  *p = *copy_type(pt);
                  p->next = nx;
                  p->name = nm;
                  found = 1;
                  break;
               }
            }
            if (!found) error_tok(pt->name, "declaration for parameter that is not in list");
         }
      }
      for (p = ty->params; p; p = p->next) if (p->is_const == 2) p->is_const = 0;
   }

   fn = find_func_global(name);
   if (fn)
   {
      if (!fn->is_function) error_tok(tok, "redeclared as a different kind of symbol");
      if (fn->is_definition && equal(tok, "{")) error_tok(tok, "redefinition of %s", name);
      if (attr->is_static) fn->is_static = 1;
      if (ty->has_proto || !fn->ty->has_proto) fn->ty = ty;
   }
   else
   {
      fn = new_gvar(name, ty);
      fn->is_function = 1;
      fn->is_static = attr->is_static;
      fn->is_definition = 0;
      if (fn->is_static) fn->asmname = format("S%d_%s", parse_file_no, name);
   }
   fn->is_definition = fn->is_definition || equal(tok, "{");
   fn->tok = ty->name;
   if (consume(&tok, tok, ";")) return tok;

   fn->ty = ty;
   current_fn = fn;
   locals = NULL;
   enter_scope();
   create_param_lvars(ty->params);
   fn->params = locals;
   tok = skip(tok, "{");
   fn->body = compound_stmt(&tok, tok);
   fn->locals = locals;
   leave_scope();
   resolve_goto_labels();
   current_fn = NULL;
   return tok;
}

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr)
{
   int first = 1;
   while (!consume(&tok, tok, ";"))
   {
      Type *ty;
      Obj *var = NULL, *o;
      char *name;
      if (!first)
      {
         if (!equal(tok, ",")) error_tok(tok, "expected ';' or ','");
         tok = tok->next;
      }
      first = 0;
      ty = declarator(&tok, tok, basety);
      if (!ty->name) error_tok(tok, "variable name omitted");
      name = get_ident(ty->name);
      if (ty->kind == TY_FUNC)
      {
         /* function declaration in a list: int a, f(void); */
         Obj *fn = find_func_global(name);
         if (!fn)
         {
            fn = new_gvar(name, ty);
            fn->is_function = 1;
            fn->is_definition = 0;
            fn->is_static = attr->is_static;
            if (fn->is_static) fn->asmname = format("S%d_%s", parse_file_no, name);
         }
         else if (ty->has_proto) fn->ty = ty;
         continue;
      }
      for (o = globals; o; o = o->next)
         if (!strcmp(o->name, name) && !o->is_local && o->asmname && strncmp(o->name, "L$", 2))
         { var = o; break; }
      if (var)
      {
         if (var->is_function) error_tok(ty->name, "redeclared as a different kind of symbol");
         if (var->ty->kind == TY_ARRAY && var->ty->array_len < 0 && ty->kind == TY_ARRAY)
            var->ty = ty;
         push_scope(name)->var = var;
      }
      else var = new_gvar(name, ty);
      var->tok = ty->name;
      if (attr->is_static && !var->is_static)
      {
         var->is_static = 1;
         var->asmname = format("S%d_%s", parse_file_no, name);
      }
      if (equal(tok, "="))
      {
         if (var->init_data) error_tok(tok, "redefinition of %s", name);
         var->is_extern = 0;
         var->is_definition = 1;
         gvar_initializer(&tok, tok->next, var);
      }
      else if (attr->is_extern)
      {
         if (!var->init_data && !var->is_tentative) var->is_extern = 1;
      }
      else
      {
         var->is_extern = 0;
         var->is_tentative = 1;
      }
   }
   return tok;
}

static int is_function(Token *tok)
{
   Type dummy, *ty;
   if (equal(tok, ";")) return 0;
   memset(&dummy, 0, sizeof(dummy));
   ty = declarator(&tok, tok, &dummy);
   if (ty->kind != TY_FUNC) return 0;
   /* definition: body or old style parameter declarations follow */
   return equal(tok, "{") || is_typename(tok);
}

Type *get_common_type_pub(Type *ty)
{
   if (ty->kind == TY_CHAR || ty->kind == TY_ENUM) return ty_int;
   if (ty->kind == TY_SHORT) return ty->is_unsigned ? ty_uint : ty_int;
   return ty;
}

static void declare_builtins(void)
{
   (void)0;
}

Obj *parse(Token *tok)
{
   scope = NULL;
   current_fn = NULL;
   enter_scope();
   declare_builtins();
   globals = NULL;
   while (tok->kind != TK_EOF)
   {
      VarAttr attr;
      Type *basety;
      if (is_asm(tok))
      {
         Node *n;
         Obj *v;
         current_fn = NULL;
         n = asm_stmt(&tok, tok);
         v = new_anon_gvar(ty_void);
         v->asmtext = n->asm_str;
         v->is_function = 0;
         continue;
      }
      if (equal(tok, ";")) { tok = tok->next; continue; }
      memset(&attr, 0, sizeof(attr));
      basety = declspec(&tok, tok, &attr);
      if (attr.is_typedef) { tok = parse_typedef(tok, basety); continue; }
      if (is_function(tok)) { tok = function(tok, basety, &attr); continue; }
      tok = global_variable(tok, basety, &attr);
   }
   all_globals = globals;
   return globals;
}
