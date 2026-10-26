"""Run synthetic local inference on the real ARM; restore the microphone build.
Private runtime remains solely in this process's memory during the workflow.
"""
import ctypes as C,hashlib,json,os,struct,subprocess,sys,time,urllib.request
from pathlib import Path
from PIL import Image
from load_local_ai import ROOT,XSCT,live_runtime
OUT=ROOT/'output/local_ai/board_validation'
OUT.mkdir(parents=True,exist_ok=True)
TMP=ROOT/'tmp/local_ai';TMP.mkdir(parents=True,exist_ok=True)
GUARD='''connect -url TCP:127.0.0.1:3121
after 300
set b [targets -target-properties -filter {name == "xc7z010"}]
if {[llength $b]!=1 || ![dict exists [lindex $b 0] jtag_cable_serial] || [dict get [lindex $b 0] jtag_cable_serial] ne "210241277120"} {error "Wrong board"}
targets -set -filter {name =~ "*Cortex-A9*#0"}
'''

def tcl(body):
    script=TMP/'verify_public.tcl';script.write_text(GUARD+body+'\ndisconnect\n',encoding='ascii')
    p=subprocess.run([str(XSCT),str(script)],capture_output=True,text=True,timeout=45)
    if p.returncode or 'PASS_LOCAL_PUBLIC' not in p.stdout:raise RuntimeError('Public board verification failed: '+p.stdout[-1200:]+p.stderr[-400:])
    return p.stdout

def symbols(app):return {p[2]:int(p[0],16) for line in (app/'symbols.log').read_text().splitlines() if len(p:=line.split())==3}

def counters(app):
    names=symbols(app);body=''
    for name,n in [('local_ai_diag',20),('speech_diag',18),('cloud_health',16),('arm_tls_diag',24)]:
        body+=f'puts "PUBLIC {name} [mrd -force -value 0x{names[name]:x} {n}]"\n'
    body+='puts "PUBLIC ui_info [mrd -force -value 0x1e080 16]"\nputs "PUBLIC display [mrd -force -value 0x1e000 16]"\nputs PASS_LOCAL_PUBLIC'
    text=tcl(body);return {p[1]:[int(v,0) for v in p[2:]] for line in text.splitlines() if len(p:=line.split())>2 and p[0]=='PUBLIC'}

def command(name):
    enum=(ROOT/'software/subtitle_ui/ui_model.h').read_text().split('enum ui_command {')[1].split('};')[0]
    number=[x.strip() for x in enum.split(',')].index(name)+1
    output=tcl(f'''set epoch [mrd -force -value 0x1e0a8]
set seq [expr {{[mrd -force -value 0x1e0c4]+1}}]
mwr -force 0x1e0c8 $epoch
mwr -force 0x1e0cc {number}
mwr -force 0x1e0c4 $seq
set end [expr {{[clock milliseconds]+5000}}]
while {{[mrd -force -value 0x1e0d0]!=$seq}} {{if {{[clock milliseconds]>$end}} {{error "No UI acknowledgement"}};after 20}}
puts "ACCEPTED [mrd -force -value 0x1e0d4]"
puts PASS_LOCAL_PUBLIC''')
    if 'ACCEPTED 1' not in output:raise RuntimeError(name+' was rejected')

def capture(name,app):
    folder=OUT/name;folder.mkdir(exist_ok=True)
    p=subprocess.run([str(XSCT),str(ROOT/'scripts/z7_nano_capture.tcl'),'210241277120',str(folder),'135848'],capture_output=True,text=True,timeout=45)
    (folder/'capture.log').write_text(p.stdout+p.stderr)
    if p.returncode or 'PASS_Z7_NANO_CAPTURE' not in p.stdout:raise RuntimeError('Capture failed')
    # Compile and call the same renderer with the exact frozen state.
    dll=OUT/'renderer.dll'
    if not dll.exists():
        files=['software/subtitle_ui/ui_model.c','software/subtitle_ui/ui_render.c','software/subtitle_ui/ui_demo.c','software/subtitle_ui/ui_host.c','output/subtitle_ui/ui_assets.c']
        subprocess.run(['gcc','-shared','-O2','-std=c11','-Wall','-Wextra','-Werror','-DZ7_LOCAL_AI','-DZ7_ARM_TLS','-DZ7_TRANSLATE_EN',
            '-I',str(ROOT/'software/subtitle_ui'),*[str(ROOT/x) for x in files],'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll));snap=(folder/'snapshot.bin').read_bytes();raw=(folder/'framebuffer.bin').read_bytes();h=struct.unpack_from('<8I',snap)
    class Stats(C.Structure):_fields_=[(str(i),C.c_uint32) for i in range(8)]
    state=C.create_string_buffer(snap[32:32+135848]);pixels=(C.c_uint32*614400)();stats=Stats()
    assert lib.ui_host_from_state(state,135848,h[4],h[5],pixels,C.byref(stats))==1
    if h[1]==2:lib.ui_host_latency(pixels,h[7],C.byref(stats))
    expected=bytes(pixels);different=sum(a!=b for a,b in zip(struct.unpack('<614400I',raw),struct.unpack('<614400I',expected)))
    Image.frombytes('RGB',(1024,600),raw,'raw','BGRX').save(folder/'screen.png')
    result=dict(status='passed' if different==0 else 'failed',pixels=614400,mismatches=different,
        elf_sha256=json.loads((app/'build.json').read_text())['ELFSHA256'],scope='real DDR pixels compared with the same native C renderer')
    (folder/'comparison.json').write_text(json.dumps(result,indent=2));assert different==0,result
    return result

def configure(app,runtime):
    env={**os.environ,'Z7_WIFI_SSID':runtime['ssid'],'Z7_WIFI_PASSWORD':runtime['password'],
        'Z7_CLOUD_KEY':runtime['key'],'Z7_CLOUD_PATH':runtime['path'],'Z7_CLOUD_WORKSPACE':runtime['workspace']}
    subprocess.run([sys.executable,str(ROOT/'scripts/configure_cloud_runtime.py'),'--yes','--mode','zhen','--symbols',str(app/'symbols.log'),'--xsdb',str(XSCT),'--env'],env=env,check=True,cwd=ROOT)
    for name in list(env):
        if name.startswith('Z7_'):env[name]=''

def load(app):subprocess.run([sys.executable,str(ROOT/'scripts/load_tts.py'),'--app-dir',str(app)],check=True,cwd=ROOT)

def main():
    runtime=live_runtime();test=ROOT/'output/local_ai/board_selftest';final=ROOT/'output/local_ai/microphone'
    record=dict(status='running',scope='synthetic PCM on actual Cortex-A9; final real microphone/cloud readiness is separate')
    try:
        load(test);print('LOCAL_BOARD_SELFTEST_LOADED',flush=True)
        deadline=time.monotonic()+25
        while True:
            observed=counters(test);diag=observed['local_ai_diag']
            if diag[5]>=2:break
            if time.monotonic()>deadline:raise RuntimeError('Local synthetic inference timed out')
            time.sleep(2)
        assert diag[6]>=1 and diag[7]>=1,diag
        assert observed['arm_tls_diag'][1]==0 and observed['cloud_health'][1]==0,'Local mode connected to cloud'
        record['synthetic_counters']=observed
        record['decode_max_ms']=diag[16]/333333
        record['feature_max_ms']=diag[17]/333333
        record['main_pixels']=capture('local_main',test)
        command('UI_OPEN_SETTINGS');command('UI_OPEN_AI')
        record['ai_pixels']=capture('ai_page',test)
        # Test the actual direct-cable pixels and mouse path with the new page.
        # Reuse an already-running trusted loopback viewer. A second UDP client
        # intentionally cannot take over its active board lease.
        def http(path,data=None):
            req=urllib.request.Request('http://127.0.0.1:8787'+path,data=json.dumps(data).encode() if data else None,
                headers={'Content-Type':'application/json','Origin':'http://127.0.0.1:8787'})
            with urllib.request.urlopen(req,timeout=8) as response:return response.read()
        def state():return json.loads(http('/state'))
        def pointer(x,y,epoch):return json.loads(http('/pointer',dict(press_x=x,press_y=y,x=x,y=y,epoch=epoch)))
        server=None
        try:state()
        except OSError:
            server=subprocess.Popen([sys.executable,str(ROOT/'scripts/remote_display_server.py'),'--local-ip','192.168.1.102'],cwd=ROOT,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            end=time.monotonic()+12
            while True:
                try:
                    current=state()
                    if current['connected'] and current['epoch']&16:break
                except OSError:pass
                if time.monotonic()>end:raise RuntimeError('PL Ethernet mirror not ready')
                time.sleep(.1)
            (OUT/'remote_ai.png').write_bytes(http('/frame.png'))
            # Current local button is already selected: select cloud and local
            # on the board via mouse, verifying each published page context.
            reply=pointer(140,164,current['epoch']);assert reply['accepted']
            previous=current['epoch'];end=time.monotonic()+8
            while state()['epoch']==previous:
                if time.monotonic()>end:raise RuntimeError('Cloud-mode view not published')
                time.sleep(.1)
            reply2=pointer(400,164,state()['epoch']);assert reply2['accepted']
            record['remote_controls']=[reply,reply2]
        finally:
            if server:server.terminate();server.wait(timeout=5)
        record['status']='passed';print('PASS_LOCAL_BOARD_INFERENCE_AND_PIXELS',flush=True)
    except Exception as error:
        record.update(status='failed',error=str(error));raise
    finally:
        (OUT/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
        print('RESTORING_REAL_MICROPHONE_BUILD',flush=True)
        load(final);configure(final,runtime);runtime.clear()
        print('PASS_LOCAL_AI_MICROPHONE_RESTORED',flush=True)
if __name__=='__main__':main()
