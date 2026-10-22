"""The local viewer's Windows Save As dialog, run on its own UI main thread."""
import datetime,json,sys,tkinter
from tkinter import filedialog

def main():
    kind=sys.argv[1]
    if kind not in ('csv','txt','json'):raise ValueError('Unknown export format')
    root=tkinter.Tk();root.withdraw();root.attributes('-topmost',True)
    try:
        path=filedialog.asksaveasfilename(parent=root,title='导出译声历史 · 选择保存位置',initialfile='译声历史_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'.'+kind,defaultextension='.'+kind,filetypes=[(kind.upper()+' 文件','*.'+kind)],confirmoverwrite=True)
        sys.stdout.reconfigure(encoding='utf-8');print(json.dumps(path or None,ensure_ascii=False))
    finally:root.destroy()
if __name__=='__main__':main()
