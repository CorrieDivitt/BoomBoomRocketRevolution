import struct,re,collections
import sys, os
SDK=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','thirdparty','rexglue-sdk','src','kernel')
xex=open(__import__('sys').argv[1],'rb').read(); img=open(sys.argv[2],'rb').read()
BASE=0x82000000
be=lambda b,o:struct.unpack('>I',b[o:o+4])[0]
le=lambda b,o:struct.unpack('<I',b[o:o+4])[0]
def tab(fn):
    t={}
    for m in re.finditer(r'XE_EXPORT\(\s*\w+,\s*(0x[0-9A-Fa-f]+),\s*(\w+),\s*(\w+)',open(fn).read()): t[int(m.group(1),16)]=(m.group(2),m.group(3))
    return t
tabs={'xam.xex':tab(os.path.join(SDK,'xam','export_table.inc')),'xboxkrnl.exe':tab(os.path.join(SDK,'xboxkrnl','export_table.inc'))}
nh=be(xex,20); hd={}
for i in range(nh):
    k,v=struct.unpack('>II',xex[24+i*8:32+i*8]); hd[k]=v
il=hd[0x103ff]; strsize=be(xex,il+4); cnt=be(xex,il+8)
names=xex[il+12:il+12+strsize].split(b'\0'); names=[n.decode() for n in names if n]
o=il+12+strsize; imports=collections.OrderedDict()
for i in range(cnt):
    ln=be(xex,o); ni,rc=struct.unpack(">HH",xex[o+0x24:o+0x28]); lib=names[ni]
    recs=[be(xex,o+0x28+4*j) for j in range(rc)]
    for r in recs:
        v=be(img,r-BASE); typ=v>>24; ordn=v&0xffff
        if typ==0: imports.setdefault(lib,[]).append((ordn,r))
    o+=ln
for lib,l in imports.items():
    print(f'## {lib}: {len(l)} imports')
    print(', '.join(tabs[lib].get(ordn,('ord%x'%ordn,))[0] for ordn,_ in sorted(l)))
# PE sections
pe=le(img,0x3c); ns=struct.unpack('<H',img[pe+6:pe+8])[0]; opt=struct.unpack('<H',img[pe+20:pe+22])[0]
so=pe+24+opt; secs=[]
print('## sections')
for i in range(ns):
    s=img[so+i*40:so+i*40+40]; nm=s[:8].rstrip(b'\0').decode(); vs,va,rs,ra=struct.unpack('<IIII',s[8:24]); fl=le(s,36)
    secs.append((nm,va,vs,fl)); print(f'  {nm:8} va=0x{BASE+va:08x} size=0x{vs:x} flags=0x{fl:08x}')
text=[s for s in secs if s[0]=='.text'][0]; t0=text[1]; t1=t0+text[2]
pd=[s for s in secs if s[0]=='.pdata']
if pd:
    p0=pd[0][1]; n=pd[0][2]//8; fn=0; tot=0
    for i in range(n):
        a=be(img,p0+i*8); w=be(img,p0+i*8+4)
        if a==0: break
        fn+=1; tot+=((w>>8)&0x3fffff)*4
    print(f'## pdata functions: {fn}, covered bytes 0x{tot:x} of text 0x{text[2]:x}')
def find(pat):
    res=[]; pb=b''.join(struct.pack('>I',x) for x in pat)
    for m in re.finditer(re.escape(pb),img[t0:t1]):
        if m.start()%4==0: res.append(BASE+t0+m.start())
    return res
helpers={'savegprlr_14':[0xF9C1FF68],'restgprlr_14':[0xE9C1FF68],'savefpr_14':[0xD9CCFF70],'restfpr_14':[0xC9CCFF70],
 'savevmx_14':[0x3960FEE0,0x7DCB61CE],'restvmx_14':[0x3960FEE0,0x7DCB60CE],'savevmx_64':[0x3960FC00,0x100B60CB],'restvmx_64':[0x3960FC00,0x100B60CB]}
print('## helper candidates')
for k,v in helpers.items(): print(' ',k,[hex(x) for x in find(v)][:4])
# instruction stats
ops=collections.Counter(); vmx128=0; bctr=0; mtctr_bctr=0; sync=0;mftb=0
prev=0
for off in range(t0,t1,4):
    ins=be(img,off); op=ins>>26; ops[op]+=1
    if op in (4,5,6) : vmx128+=1
    if ins==0x4E800420: bctr+=1; 
    if ins==0x4E800420 and (prev&0xFC1FFFFF)==0x7C0903A6: mtctr_bctr+=1
    if (ins&0xFC0007FE)==0x7C0002E6 or (ins&0xFC1FFFFE)==0x7C0C42E6: mftb+=1
    prev=ins
print(f'## text words {(t1-t0)//4}, opcode4/5/6 (VMX/VMX128) {vmx128}, bctr {bctr}, mtctr+bctr {mtctr_bctr}, mftb {mftb}')
print('  top opcodes',ops.most_common(12))
