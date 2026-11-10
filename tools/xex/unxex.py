import struct,sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
d=open(sys.argv[1],'rb').read()
be=lambda b,o:struct.unpack('>I',b[o:o+4])[0]
pe_off=be(d,8); sec=be(d,16); nh=be(d,20)
hd={}
for i in range(nh):
    k,v=struct.unpack('>II',d[24+i*8:32+i*8]); hd[k]=v
ff=hd[0x3ff]
win=be(d,ff+8); first=be(d,ff+12)
filekey=d[sec+0x150:sec+0x160]
retail=bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
dec=lambda key,data,mode: Cipher(algorithms.AES(key),mode).decryptor().update(data)
sess=dec(retail,filekey,modes.ECB())
body=d[pe_off:]
body=body[:len(body)//16*16]
p=dec(sess,body,modes.CBC(b'\0'*16))
out=bytearray(); pos=0; size=first
while size:
    nxt=pos+size
    nsize=be(p,pos); pos+=24
    while True:
        cs=struct.unpack('>H',p[pos:pos+2])[0]; pos+=2
        if cs==0: break
        out+=p[pos:pos+cs]; pos+=cs
    pos=nxt; size=nsize
open(sys.argv[2],'wb').write(out)
wb=win.bit_length()-1
print(wb, be(d,sec+4), len(out))
