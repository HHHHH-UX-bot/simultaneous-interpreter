"""Check the eight requested physical taps using bound CPU0/CPU1 evidence."""
import json
import run_z7_nano as base
import run_z7_nano_display as display
import run_z7_nano_usb_host as host

def main():
    out=base.OUT/'usb'
    run=json.loads((out/'host_run.json').read_text())
    active=json.loads((display.OUT/'active_app.json').read_text())
    build=json.loads((out/'host_build.json').read_text())
    app=display.checked_app()
    if run['status']!='passed' or run['input_hashes']!=host.inputs() or run['elf_sha256']!=build['elf_sha256'] or \
       active['status']!='ready' or active['elf_sha256']!=app['elf_sha256'] or \
       run['display_program_attempt']!=active['program_attempt']:
        raise RuntimeError('Current Host/UI execution binding required')
    feedback=run['feedback'];touch=run['touch']
    expected={'start':1,'pause':1,'resume':1,'clear':1,'font_minus':1,'font_plus':1,'toggle_source':2}
    passed=feedback['accepted']==expected and feedback['ignored']==0 and feedback['last_command']==0 and \
        feedback['read_taps']==8 and touch['tap_write']==8 and touch['tap_read']==8 and \
        touch['downs']==8 and touch['ups']==8 and not touch['pressed'] and touch['dropped_taps']==0
    result={'status':'passed' if passed else 'failed','screen_acceptance':'pending',
        'scope':'physical taps delivered to all seven existing controls; visual response is separately confirmed',
        'host_attempt':run['attempt'],'host_elf_sha256':run['elf_sha256'],
        'display_program_attempt':active['program_attempt'],'display_run_attempt':active['attempt'],
        'display_elf_sha256':app['elf_sha256'],'touch_sha256':run['touch_sha256'],
        'feedback_sha256':run['feedback_sha256'],'ui_test_sha256':base.sha(out/'ui_touch_tests.json'),
        'expected_controls':expected,'feedback':feedback,'touch':touch}
    display.save(out/'touch_buttons.json',result)
    if not passed:raise RuntimeError('Physical controls differ from requested eight taps; inspect touch_buttons.json')
    print('PASS_TOUCH_BUTTONS',feedback['accepted'])

if __name__=='__main__':main()
