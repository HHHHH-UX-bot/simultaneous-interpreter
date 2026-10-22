"""Chosen-path export round trip; does not automate native desktop dialogs."""
import contextlib,io,json,sys,tempfile
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import history_save_dialog as dialog,remote_display_server as server
known=dict(exported_at='2026-10-22T16:00:00+08:00',count=1,discarded=0,epoch=2,final_count=1,records=[dict(number=1,direction='中译英',epoch=2,segment_id=1,revision=1,source_revision=1,source='测试指定保存位置。',translation='Test the selected save location.')])
class Board:
    def history(self):return known

def main():
    with tempfile.TemporaryDirectory() as folder:
        for kind in ('csv','txt','json'):
            target=Path(folder)/('用户指定目录_历史.'+kind)
            result=type('Result',(),dict(stdout=json.dumps(str(target),ensure_ascii=False).encode('utf-8')))()
            with patch.object(server.subprocess,'run',return_value=result):saved=server.save_history_dialog(Board(),kind)
            assert saved['path']==str(target) and target.read_bytes()==server.history_file(known,kind)[0]
        with patch.object(server.subprocess,'run',return_value=type('Result',(),dict(stdout=b'null'))()):assert server.save_history_dialog(Board(),'csv')==dict(cancelled=True)
        with patch.object(dialog.tkinter,'Tk'),patch.object(dialog.filedialog,'asksaveasfilename',return_value=str(target)) as choose,patch.object(sys,'argv',['dialog','json']):
            capture=io.StringIO();capture.reconfigure=lambda **kwargs:None
            with contextlib.redirect_stdout(capture):dialog.main()
            assert choose.call_args.kwargs['confirmoverwrite'] is True and choose.call_args.kwargs['defaultextension']=='.json'
            assert json.loads(capture.getvalue())==str(target)
    print('PASS_HISTORY_SAVE CSV/TXT/JSON, Unicode chosen path, cancellation, overwrite dialog option; native chooser mocked')
if __name__=='__main__':main()
