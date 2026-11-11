# Lists or unpacks an Xbox 360 content package (LIVE/PIRS, e.g. a Boom Boom Rocket DLC pack).
#   python3 tools/stfs/stfs.py <package>            list the files
#   python3 tools/stfs/stfs.py <package> <folder>   unpack them into <folder>
# Unpacked game files are copyrighted: keep them out of the repo.
import struct,sys,os
class Stfs:
    def __init__(s,path):
        s.d=open(path,'rb').read(); d=s.d
        s.magic=d[:4]; s.header_size=struct.unpack('>I',d[0x340:0x344])[0]
        s.base=(s.header_size+0xFFF)&0xFFFFF000
        s.sep=d[0x37B]; s.ft_count=struct.unpack('<H',d[0x37C:0x37E])[0]
        s.ft_block=d[0x37E]|(d[0x37F]<<8)|(d[0x380]<<16)
        s.shift=0 if ((s.base>>12)==0xB or (s.sep&1)==1) else 1
        s.title=d[0x411:0x411+0x80].decode('utf-16-be','ignore').rstrip('\0')
    def adj(s,b):
        r=(((b+0xAA)//0xAA)<<s.shift)+b
        if b<0xAA: return r
        if b<0x70E4: return r+(((b+0x70E4)//0x70E4)<<s.shift)
        return (1<<s.shift)+r+(((b+0x70E4)//0x70E4)<<s.shift)
    def off(s,b): return s.base+(s.adj(b)<<12)
    def hash_off(s,b):
        step=0xAB if s.shift==0 else 0xAC
        n=(b//0xAA)*step
        if b>=0xAA: n+=((b//0x70E4)+1)<<s.shift
        if b>=0x70E4: n+=1<<s.shift
        return s.base+(n<<12)+(b%0xAA)*0x18
    def next(s,b):
        o=s.hash_off(b); e=s.d[o:o+0x18]
        return (e[0x15]<<16)|(e[0x16]<<8)|e[0x17]
    def chain(s,start,count):
        out=[];b=start
        for i in range(count):
            out.append(b); b=s.next(b)
        return out
    def read(s,start,count,size=None):
        buf=b''.join(s.d[s.off(b):s.off(b)+0x1000] for b in s.chain(start,count))
        return buf if size is None else buf[:size]
    def files(s):
        t=s.read(s.ft_block,s.ft_count); out=[]
        for i in range(0,len(t),0x40):
            e=t[i:i+0x40]
            if e[0]==0: break
            fl=e[0x28]; name=e[:fl&0x3F].decode('latin1')
            blocks=e[0x29]|(e[0x2A]<<8)|(e[0x2B]<<16); start=e[0x2F]|(e[0x30]<<8)|(e[0x31]<<16)
            size=struct.unpack('>I',e[0x34:0x38])[0]; parent=struct.unpack('>H',e[0x32:0x34])[0]
            out.append(dict(name=name,dir=bool(fl&0x80),consec=bool(fl&0x40),blocks=blocks,start=start,size=size,parent=parent))
        return out
    def extract(s,f):
        if f['consec']:
            buf=b''.join(s.d[s.off(b):s.off(b)+0x1000] for b in range(f['start'],f['start']+f['blocks']))
            return buf[:f['size']]
        return s.read(f['start'],f['blocks'],f['size'])
if __name__=='__main__':
    p=Stfs(sys.argv[1]); print(p.magic,hex(p.header_size),hex(p.base),'sep',p.sep,'shift',p.shift,'title',repr(p.title))
    fs=p.files()
    for f in fs: print(f['name'],f['size'],f['blocks'],f['start'],'dir' if f['dir'] else '', 'c' if f['consec'] else '')
    if len(sys.argv)>2:
        os.makedirs(sys.argv[2],exist_ok=True)
        for f in fs:
            if not f['dir']: open(os.path.join(sys.argv[2],f['name']),'wb').write(p.extract(f))
