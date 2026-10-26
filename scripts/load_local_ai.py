"""Load matching FPGA/CPU0/CPU1; optionally reuse the board's volatile config.

Only the fixed NCF1/CCL1 mailboxes are read. Private output is captured in
process memory and discarded; secrets are never put in files or argv.
"""
import argparse,json,os,re,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
XSCT=Path('C:/Xilinx/SDK/2018.3/bin/xsct.bat')

def windows_wifi():
    r=subprocess.run(['netsh','wlan','show','interfaces'],capture_output=True)
    text=r.stdout.decode('utf-8',errors='replace')
    match=re.search(r'^\s*SSID\s*:\s*(.+)$',text,re.M)
    if not match:raise RuntimeError('Current Windows Wi-Fi profile unavailable')
    ssid=match.group(1).strip()
    r=subprocess.run(['netsh','wlan','show','profile','name='+ssid,'key=clear'],capture_output=True)
    text=r.stdout.decode('utf-8',errors='replace')
    match=re.search(r'^\s*(?:Key Content|关键内容)\s*:\s*(.+)$',text,re.M)
    if not match:raise RuntimeError('Wi-Fi password unavailable from the current Windows profile')
    return ssid,match.group(1).strip()

def live_runtime():
    script=ROOT/'tmp/local_ai/read_runtime.tcl';script.parent.mkdir(parents=True,exist_ok=True)
    script.write_text('''connect -url TCP:127.0.0.1:3121
after 500
set b [targets -target-properties -filter {name == "xc7z010"}]
if {[llength $b]!=1 || ![dict exists [lindex $b 0] jtag_cable_serial] || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong target"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
puts "PRIVATE_NET [mrd -force -value 0x1e180 32]"
puts "PRIVATE_CLOUD [mrd -force -value 0x1e200 85]"
disconnect
''',encoding='ascii')
    r=subprocess.run([str(XSCT),str(script)],capture_output=True,text=True,timeout=30)
    # Never include raw output in an error or a log.
    values={}
    for line in r.stdout.splitlines():
        for name in ('NET','CLOUD'):
            if line.startswith('PRIVATE_'+name+' '):
                words=[int(x,0) for x in line.split()[1:]]
                values[name]=struct.pack('<'+'I'*len(words),*words)
    if r.returncode or set(values)!=set(('NET','CLOUD')):raise RuntimeError('No reusable board runtime configuration')
    n,c=values['NET'],values['CLOUD'];head=struct.unpack_from('<5I',n)
    if head[0]!=0x3146434e or struct.unpack_from('<I',c)[0]!=0x314c4343:
        raise RuntimeError('Board runtime mailboxes are empty or do not match this firmware')
    if 1<=head[3]<=32 and 8<=head[4]<=64:
        ssid=n[20:20+head[3]].decode();password=n[53:53+head[4]].decode()
    else:
        # Firmware wipes the mailbox after copying Wi-Fi into private RAM.
        # Use only the currently connected Windows profile, in memory.
        ssid,password=windows_wifi()
    path=c[4:100].split(b'\0')[0].decode();workspace=c[100:148].split(b'\0')[0].decode();key=c[148:340].split(b'\0')[0].decode()
    if not key or workspace!='ws-zckg39zhbiii3yrv':raise RuntimeError('Board runtime belongs to another cloud workspace')
    return dict(ssid=ssid,password=password,key=key,path=path,workspace=workspace)

def main():
    p=argparse.ArgumentParser();p.add_argument('--app-dir',type=Path,default=ROOT/'output/local_ai/microphone')
    p.add_argument('--reuse-runtime',action='store_true',help='keep current Wi-Fi/API config in memory across the reload')
    p.add_argument('--configure',action='store_true',help='prompt privately when no runtime is reused')
    p.add_argument('--offline',action='store_true',help='start the fixed-phrase local engine without Wi-Fi or API credentials')
    a=p.parse_args();app=a.app_dir.resolve();runtime=live_runtime() if a.reuse_runtime else None
    subprocess.run([sys.executable,str(ROOT/'scripts/load_tts.py'),'--app-dir',str(app)],check=True,cwd=ROOT)
    build=json.loads((app/'build.json').read_text())
    if a.offline:
        from verify_local_board import command
        command('UI_AI_LOCAL');command('UI_START')
        print('PASS_LOCAL_AI_OFFLINE_STARTED');return
    if runtime or a.configure:
        args=[sys.executable,str(ROOT/'scripts/configure_cloud_runtime.py'),'--yes','--mode',build['Mode'],'--symbols',str(app/'symbols.log'),'--xsdb',str(XSCT)]
        if runtime:
            env={**os.environ,'Z7_WIFI_SSID':runtime['ssid'],'Z7_WIFI_PASSWORD':runtime['password'],
                'Z7_CLOUD_KEY':runtime['key'],'Z7_CLOUD_PATH':runtime['path'],'Z7_CLOUD_WORKSPACE':runtime['workspace']}
            runtime.clear();args.append('--env')
            # A local self-test remains offline. Retain the config only across
            # the parent process's optional verification/restore workflow.
            subprocess.run(args,check=True,env=env,cwd=ROOT)
            for name in list(env):
                if name.startswith('Z7_'):env[name]=''
        else:subprocess.run(args,check=True,cwd=ROOT)
    print('PASS_LOCAL_AI_LOADED')
if __name__=='__main__':main()
