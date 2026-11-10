import struct,sys
d=open(sys.argv[1],'rb').read()
be=lambda o:struct.unpack('>I',d[o:o+4])[0]
magic,flags,pe_off,_,sec_off,nhdr=struct.unpack('>4sIIIII',d[:24])
print(magic,hex(flags),'pe_off',hex(pe_off),'sec',hex(sec_off),'nhdr',nhdr)
hdrs={}
for i in range(nhdr):
    k,v=struct.unpack('>II',d[24+i*8:32+i*8]); hdrs[k]=v; print(' key',hex(k),hex(v))
ff=hdrs[0x3ff]; print('fileformat size',be(ff),'enc',struct.unpack('>H',d[ff+4:ff+6])[0],'comp',struct.unpack('>H',d[ff+6:ff+8])[0])
# static libs
sl=hdrs.get(0x200ff)
if sl:
    n=(be(sl)-4)//16
    for i in range(n):
        o=sl+4+i*16; name=d[o:o+8].rstrip(b'\0').decode(); a,b,c,e=struct.unpack('>HHHH',d[o+8:o+16])
        print('  staticlib',name,f'{a}.{b}.{c}.{e&0x7fff}', 'approval',e>>13)
il=hdrs.get(0x103ff)
if il:
    size=be(il); strsize=be(il+4); cnt=be(il+8); s=d[il+12:il+12+strsize]
    names=[x.decode() for x in s.split(b'\0') if x]; print('  imports libs',names)
    o=il+12+strsize
    for i in range(cnt):
        ln=be(o); ver=be(o+0x20); minv=be(o+0x24); nrec=struct.unpack('>H',d[o+0x2a:o+0x2c])[0]
        print('   lib',i,'ver',hex(ver),'records',nrec); o+=ln
ex=hdrs.get(0x40006)
if ex:
    mid,ver,bver,tid=struct.unpack('>IIII',d[ex:ex+16]); print('  exec media',hex(mid),'ver',hex(ver),'titleid',hex(tid))
pn=hdrs.get(0x183ff)
if pn: print('  pe name',d[pn+4:pn+be(pn)].rstrip(b'\0'))
rs=hdrs.get(0x2ff)
if rs:
    n=(be(rs)-4)//16
    for i in range(n):
        o=rs+4+i*16; print('  resource',d[o:o+8].rstrip(b'\0'),hex(be(o+8)),hex(be(o+12)))
print('entry',hex(hdrs[0x10100]),'base',hex(hdrs[0x10201]),'stack',hex(hdrs.get(0x20200,0)))
print('tls', hdrs.get(0x20104) and d[hdrs[0x20104]:hdrs[0x20104]+16].hex())
print('image_size',hex(be(sec_off+4)),'flags',hex(be(sec_off+0x10c)),'region',hex(be(sec_off+0x178)),'pages',be(sec_off+0x180))
