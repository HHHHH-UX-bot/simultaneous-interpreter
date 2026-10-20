"""Bind the existing CPU1 touch lifecycle to an explicitly loaded cloud app.

The cloud app directory must contain build.json and a load.json recording the
actual successful z7_nano_audio.tcl run (ELF/bit hashes and its result file).
This does not configure PL, load CPU0 or change Wi-Fi/cloud credentials.
"""
import argparse
import json
from datetime import datetime,timezone
from pathlib import Path
import run_z7_nano as base
import run_z7_nano_usb_host as host

ROOT=base.ROOT

def save(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('action',choices=('start','sample','stop'))
    p.add_argument('--app-dir',type=Path,required=True)
    p.add_argument('--xsdb',type=Path,default=base.XSDB)
    a=p.parse_args();app=a.app_dir.resolve()
    loaded=json.loads((app/'load.json').read_text())
    load_id=base.sha(app/'load.json')
    actual=json.loads((ROOT/loaded['result']).read_text())
    if actual['display_ok']!=1 or \
       actual['cable_serial']!=loaded['cable_serial'] or loaded['result_sha256']!=base.sha(ROOT/loaded['result']):
        raise RuntimeError('Matching successful touch-enabled CPU0 load required')
    manifest=app/'touch_live.json'
    previous=json.loads(manifest.read_text()) if manifest.exists() else {}
    generation=0
    if a.action!='start':
        if previous.get('load_id')!=load_id or previous.get('status') not in ('running','unconfirmed'):
            raise RuntimeError('Matching running or explicitly owned unconfirmed Host required')
        generation=previous['generation']
    else:
        if previous.get('load_id')==load_id and previous.get('status') in ('running','unconfirmed','start_unconfirmed'):
            raise RuntimeError('Sample or stop the current Host before restarting')
        build=json.loads((app/'build.json').read_text())
        if not build['Touch'] or build['ELFSHA256']!=base.sha(app/'cloud_app.elf') or loaded['elf_sha256']!=build['ELFSHA256']:
            raise RuntimeError('Current loaded touch-enabled CPU0 build required')
        if any(base.sha(ROOT/name)!=digest for name,digest in build['InputSHA256'].items()):
            raise RuntimeError('Cloud build inputs changed')
        host_build=json.loads((host.OUT/'host_build.json').read_text())
        if host_build['status']!='passed' or host_build['input_hashes']!=host.inputs() or \
           host_build['elf_sha256']!=base.sha(host.OUT/'host.elf'):
            raise RuntimeError('Current CPU1 host build required')
    folder=app/'touch'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    folder.mkdir(parents=True)
    record={'status':'unconfirmed','action':a.action,'load_id':load_id,
            'generation':generation,'cable_serial':loaded['cable_serial'],
            'attempt':folder.relative_to(ROOT).as_posix(),
            'cpu0_elf_sha256':loaded['elf_sha256'],
            'cpu1_elf_sha256':host_build['elf_sha256'] if a.action=='start' else previous['cpu1_elf_sha256']}
    with base.hardware_lock():
        save(manifest,record)
        try:
            base.run([a.xsdb,ROOT/'scripts/z7_nano_usb_live.tcl',a.action,
                      loaded['cable_serial'],host.OUT/'host.elf',folder,generation],
                     folder/'run','PASS_Z7_NANO_USB_LIVE_'+a.action.upper(),40)
        except BaseException:
            if (folder/'owned.json').exists():
                owner=json.loads((folder/'owned.json').read_text())
                record['generation']=owner.get('generation',generation)
                if a.action=='start' and 'generation' not in owner:record['status']='start_unconfirmed'
                # base.run terminates the child on timeout/interruption, so its
                # Tcl catch may not run. Stop only the generation we own.
                if not (folder/'cleanup.json').exists():
                    try:
                        if 'generation' not in owner and a.action=='start':
                            base.run([a.xsdb,ROOT/'scripts/z7_nano_usb_cleanup.tcl',loaded['cable_serial'],folder/'cleanup.json'],
                                     folder/'cleanup_run','PASS_Z7_NANO_USB_CLEANUP',20)
                        else:
                            cleanup=folder/'cleanup';cleanup.mkdir()
                            base.run([a.xsdb,ROOT/'scripts/z7_nano_usb_live.tcl','stop',loaded['cable_serial'],'',cleanup,record['generation']],
                                     cleanup/'run','PASS_Z7_NANO_USB_LIVE_STOP',20)
                            stopped=json.loads((cleanup/'result.json').read_text())
                            if not stopped['host_cpu'].startswith('Stopped:'):raise RuntimeError('Host stop unconfirmed')
                            save(folder/'cleanup.json',{'status':'passed','usb_dma':'Halted','host_cpu':'Stopped'})
                    except Exception as error:
                        print('Owned USB cleanup not confirmed: '+str(error))
                if (folder/'cleanup.json').exists():record['status']='stopped_after_failure'
                save(manifest,record)
            else:
                record['status']='failed_before_ownership'
                save(manifest,previous or record)
            save(folder/'failure.json',record);raise
        result=json.loads((folder/'result.json').read_text())
        record.update(result,status='stopped' if a.action=='stop' else 'running')
        save(folder/'result.json',record);save(manifest,record)
    print('PASS_CLOUD_TOUCH_'+a.action.upper())

if __name__=='__main__':main()
