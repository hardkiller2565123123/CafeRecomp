param([Parameter(Mandatory=$true)][string]$PackPath, [string]$GuestLogPath)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class BotwEffectInspector {
    static uint U(byte[] b, int p) {
        return ((uint)b[p]<<24)|((uint)b[p+1]<<16)|((uint)b[p+2]<<8)|b[p+3];
    }
    static byte[] Decode(byte[] b, int start, int end) {
        var o = new byte[checked((int)U(b,start+4))];
        int p=start+16, q=0, bits=0, flags=0;
        while(q<o.Length) {
            if(bits==0) { if(p>=end) throw new Exception("Truncated flags"); flags=b[p++]; bits=8; }
            if((flags&128)!=0) { if(p>=end) throw new Exception("Truncated literal"); o[q++]=b[p++]; }
            else {
                if(p+1>=end) throw new Exception("Truncated match");
                int a=b[p++], c=b[p++], len=a>>4, from=q-(((a&15)<<8)|c)-1;
                if(len==0) { if(p>=end) throw new Exception("Truncated length"); len=b[p++]+18; } else len+=2;
                if(from<0 || q+len>o.Length) throw new Exception("Invalid match");
                while(len-->0) o[q++]=o[from++];
            }
            flags<<=1; bits--;
        }
        return o;
    }
    public static void Inspect(string path, string logPath) {
        var b=File.ReadAllBytes(path);
        int h=(b[4]<<8)|b[5], count=(b[h+6]<<8)|b[h+7];
        int names=h+12+count*16+8, data=(int)U(b,12);
        for(int i=0;i<count;i++) {
            int n=h+12+i*16, s=names+(int)(U(b,n+4)&0xffffff)*4, e=s;
            while(b[e]!=0) e++;
            string name=Encoding.ASCII.GetString(b,s,e-s);
            if(name!="Effect/GameResident.sesetlist") continue;
            var asset=Decode(b,data+(int)U(b,n+8),data+(int)U(b,n+12));
            Console.WriteLine("{0}: decoded {1} bytes",name,asset.Length);
            for(int root=0;root+32<asset.Length;root+=4) {
                if(U(asset,root)!=0x54455852 || U(asset,root+4)!=0x30) continue;
                Console.WriteLine("TEXR root={0:X8} child={1:X8}",root,U(asset,root+8));
                int node=unchecked(root+(int)U(asset,root+8));
                var visited=new System.Collections.Generic.HashSet<int>();
                for(int k=0;k<4096;k++) {
                    if(node<0 || node+32>asset.Length || !visited.Add(node)) {
                        throw new Exception(String.Format("Invalid node={0:X8} index={1}",node,k));
                    }
                    uint next=U(asset,node+12), tag=U(asset,node);
                    if(k<3 || next==uint.MaxValue || tag!=0x47583242)
                        Console.WriteLine("node[{0}]={1:X8} tag={2:X8} next={3:X8}",k,node,tag,next);
                    if(next==uint.MaxValue) break;
                    node=unchecked(node+(int)next);
                }
                if(!String.IsNullOrEmpty(logPath)) {
                    long guestBase=-1;
                    int compared=0;
                    using(var stream=new FileStream(logPath,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
                    using(var reader=new StreamReader(stream)) {
                    string line;
                    while((line=reader.ReadLine())!=null) {
                        var m=System.Text.RegularExpressions.Regex.Match(line,@"GX2B input root=([0-9A-F]+)");
                        if(m.Success) {
                            if(guestBase>=0) break; // only the first TEXR snapshot
                            guestBase=Convert.ToInt64(m.Groups[1].Value,16)-root;
                            continue;
                        }
                        m=System.Text.RegularExpressions.Regex.Match(line,@"texture node=([0-9A-F]+) words=([0-9A-F,]+)");
                        if(!m.Success || guestBase<0) continue;
                        long offset=Convert.ToInt64(m.Groups[1].Value,16)-guestBase;
                        if(offset<0 || offset+32>asset.Length) throw new Exception("Guest link leaves asset: "+line);
                        string[] words=m.Groups[2].Value.Split(',');
                        for(int w=0;w<8;w++) {
                            uint actual=Convert.ToUInt32(words[w],16), expected=U(asset,(int)offset+w*4);
                            if(actual!=expected) {
                                Console.WriteLine("MISMATCH guest={0:X8} asset={1:X8} word={2} expected={3:X8} actual={4:X8}",
                                    guestBase+offset,offset,w,expected,actual);
                                uint second=Convert.ToUInt32(words[1],16);
                                for(int p=0;p+8<b.Length;p++)
                                    if(U(b,p)==Convert.ToUInt32(words[0],16) && U(b,p+4)==second)
                                        Console.WriteLine("Observed bytes in pack at {0:X8}, compressed payload offset {1:X8}",p,p-data-(int)U(b,n+8));
                                for(int p=0;p+8<asset.Length;p++)
                                    if(U(asset,p)==Convert.ToUInt32(words[0],16) && U(asset,p+4)==second)
                                        Console.WriteLine("Observed bytes in decoded asset at {0:X8}",p);
                                return;
                            }
                        }
                        compared++;
                    }
                    }
                    Console.WriteLine("Matched {0} guest record headers",compared);
                }
                break;
            }
        }
    }
}
'@
[BotwEffectInspector]::Inspect((Resolve-Path -LiteralPath $PackPath).Path, $GuestLogPath)
