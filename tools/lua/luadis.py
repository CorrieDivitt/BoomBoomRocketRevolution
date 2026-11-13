import struct,sys
d=open(sys.argv[1],'rb').read()
assert d[:4]==b'\x1bLua'
ver,fmt,endian,si,ss,sinst,snum,integral=d[4:12]
E_='<' if endian else '>'
pos=12
def rd(n):
    global pos
    b=d[pos:pos+n]; pos+=n; return b
def u32(): return struct.unpack(E_+'I',rd(4))[0]
def sz(): return struct.unpack(E_+('I' if ss==4 else 'Q'),rd(ss))[0]
def s():
    n=sz()
    if n==0: return None
    b=rd(n); return b[:-1].decode('latin1')
def num():
    b=rd(snum)
    if integral: return struct.unpack(E_+('i' if snum==4 else 'q'),b)[0]
    return struct.unpack(E_+('f' if snum==4 else 'd'),b)[0]
OPS="MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE SELF ADD SUB MUL DIV MOD POW UNM NOT LEN CONCAT JMP EQ LT LE TEST TESTSET CALL TAILCALL RETURN FORLOOP FORPREP TFORLOOP SETLIST CLOSE CLOSURE VARARG".split()
def func(depth,path):
    src=s(); line=u32(); last=u32(); nups,nparams,vararg,maxstack=rd(4)
    n=u32(); code=[u32() for _ in range(n)]
    n=u32(); K=[]
    for _ in range(n):
        t=rd(1)[0]
        if t==0: K.append(None)
        elif t==1: K.append(bool(rd(1)[0]))
        elif t==3: K.append(num())
        elif t==4: K.append(s())
        else: raise Exception('ktype %d'%t)
    n=u32(); subs=[]
    out=[]
    def rk(x): return ('K(%r)'%(K[x-256],)) if x>=256 else 'R%d'%x
    for i,ins in enumerate(code):
        op=ins&0x3f; a=(ins>>6)&0xff; c=(ins>>14)&0x1ff; b=(ins>>23)&0x1ff; bx=(ins>>14)&0x3ffff; sbx=bx-131071
        name=OPS[op] if op<len(OPS) else 'OP%d'%op
        if name in('LOADK','GETGLOBAL','SETGLOBAL'): t='R%d %r'%(a,K[bx])
        elif name in('GETTABLE','SELF'): t='R%d = R%d[%s]'%(a,b,rk(c))
        elif name=='SETTABLE': t='R%d[%s] = %s'%(a,rk(b),rk(c))
        elif name in('EQ','LT','LE'): t='%d %s %s'%(a,rk(b),rk(c))
        elif name in('JMP','FORLOOP','FORPREP'): t='-> %d'%(i+1+sbx)
        elif name=='CLOSURE': t='R%d f%d'%(a,bx)
        else: t='A%d B%d C%d'%(a,b,c)
        out.append('%4d %-9s %s'%(i,name,t))
    print('=== function %s (line %d, params %d)'%(path,line,nparams)); print('\n'.join(out))
    for j in range(n): func(depth+1,path+'.%d'%j)
    n=u32(); rd(4*n)       # lineinfo
    n=u32()
    for _ in range(n): s(); u32(); u32()
    n=u32()
    for _ in range(n): s()
print('header',ver,fmt,endian,si,ss,sinst,snum,integral)
func(0,'main')
