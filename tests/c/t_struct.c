#include <stdio.h>
#include <string.h>
struct point { int x, y; };
struct rect { struct point a, b; char name[8]; };
typedef struct node { int val; struct node *next; } Node;
union u { long l; int i[2]; char c[4]; };
struct bits { unsigned a : 3; unsigned b : 5; int c : 4; unsigned d : 1; unsigned e : 12; };
enum color { RED, GREEN = 5, BLUE };

struct point mkpoint(int x, int y) { struct point p; p.x = x; p.y = y; return p; }
int area(struct rect r) { return (r.b.x - r.a.x) * (r.b.y - r.a.y); }
void move(struct rect *r, int dx) { r->a.x += dx; r->b.x += dx; }
struct rect gr = { { 1, 2 }, { 11, 22 }, "global" };
struct point pts[3] = { { 1, 1 }, { 2, 4 }, { 3, 9 } };

int main(void)
{
   struct rect r;
   struct point p, q;
   Node n1, n2, n3, *np;
   union u un;
   struct bits bf;
   enum color c = BLUE;
   int i;
   r.a = mkpoint(0, 0);
   r.b = mkpoint(10, 5);
   strcpy(r.name, "box");
   printf("%d %d %d %s\n", area(r), r.b.x, r.b.y, r.name);
   move(&r, 3);
   printf("%d %d\n", r.a.x, r.b.x);
   p = r.b;
   q = p;
   q.x = 99;
   printf("%d %d %d\n", p.x, q.x, q.y);
   printf("%d %d %s %d\n", gr.a.y, gr.b.x, gr.name, area(gr));
   for (i = 0; i < 3; i++) printf("(%d,%d) ", pts[i].x, pts[i].y);
   printf("\n");
   n1.val = 1; n2.val = 2; n3.val = 3;
   n1.next = &n2; n2.next = &n3; n3.next = NULL;
   for (np = &n1; np; np = np->next) printf("%d ", np->val);
   printf("\n");
   un.l = 0x11223344L;
   printf("%x %x %x %x\n", un.i[0], un.i[1], un.c[0], un.c[3]);
   printf("%d %d %d\n", (int)sizeof(struct rect), (int)sizeof(union u), (int)sizeof(Node));
   memset(&bf, 0, sizeof bf);
   bf.a = 5; bf.b = 17; bf.c = -3; bf.d = 1; bf.e = 4000;
   printf("%u %u %d %u %u\n", bf.a, bf.b, bf.c, bf.d, bf.e);
   bf.a++; bf.b += 20; bf.c = 7; bf.e = bf.e + 100;
   printf("%u %u %d %u %u\n", bf.a, bf.b, bf.c, bf.d, bf.e);
   printf("%d %d %d\n", RED, GREEN, c);
   {
      struct point *pp = pts;
      pp[1].y = 40;
      (pp + 2)->x = 30;
      printf("%d %d\n", pts[1].y, pts[2].x);
   }
   {
      struct { char a; long b; char c; } s;
      s.a = 'A'; s.b = 123456L; s.c = 'C';
      printf("%c %ld %c %d\n", s.a, s.b, s.c, (int)sizeof s);
   }
   {
      struct rect r2 = r;
      r2.name[0] = 'B';
      printf("%s %s\n", r.name, r2.name);
   }
   return 0;
}
