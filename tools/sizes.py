import sys,re
sym=open(sys.argv[1]).read().split('\n')[2:]
items=[]
for l in sym:
    if len(l)<7: continue
    try: v=int(l[2:6],16)
    except: continue
    n=l[7:].strip()
    if n.startswith('L') and (n[1:].isdigit() or n.startswith('L$')): continue
    if n.startswith('__') and n[2:3]!='_' : pass
    items.append((v,n))
items.sort()
res=[]
for i in range(len(items)-1):
    v,n=items[i]
    if 0x2000<=v<0x8000:
        res.append((items[i+1][0]-v,n))
res.sort(reverse=True)
tot=0
for s,n in res[:int(sys.argv[2]) if len(sys.argv)>2 else 40]:
    print("%6d %s"%(s,n))
