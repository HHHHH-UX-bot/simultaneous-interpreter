"""Exercise actual export assembly and files, including bilingual paragraphs."""
import csv,io,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from remote_display_server import compose_history,history_file
class ExportTest(unittest.TestCase):
 def fixture(self):
  pairs=[('我们开始开会。','We will begin the meeting.'),('今天讨论项目进度。','Today we will discuss project progress.'),('另外，测试已经通过。','In addition, the tests passed.'),('最后，请记录结论。','Finally, please record the conclusion.')]
  records=[dict(number=i+1,direction='中译英',source=s,translation=t,epoch=1,segment_id=i+1,source_revision=1,revision=1) for i,(s,t) in enumerate(pairs)]
  return dict(exported_at='2026-10-23T21:00:00+08:00',count=4,discarded=2,epoch=1,final_count=6,records=records)
 def test_three_formats(self):
  h=self.fixture();full=compose_history(h['records']);self.assertEqual(full['source'],'我们开始开会。今天讨论项目进度。\n\n另外，测试已经通过。最后，请记录结论。');self.assertIn('meeting. Today',full['translation'])
  data,_=history_file(h,'json');j=json.loads(data);self.assertEqual(j['records'],h['records']);self.assertEqual(j['full_text'],full);self.assertEqual(list(j)[-1],'full_text');self.assertNotIn('full_text',h)
  data,_=history_file(h,'csv');rows=list(csv.reader(io.StringIO(data.decode('utf-8-sig'))));self.assertTrue(all(len(r)==10 for r in rows));self.assertEqual(rows[-1][0],'完整文本');self.assertEqual(rows[-1][2:4],[full['source'],full['translation']]);self.assertEqual(rows[1][2],h['records'][0]['source'])
  data,_=history_file(h,'txt');text=data.decode('utf-8-sig');self.assertTrue(text.endswith(full['translation']+'\r\n'));self.assertIn('完整原文：',text);self.assertIn('完整译文：',text)
 def test_boundaries_and_no_dedup(self):
  h=self.fixture();r=h['records'];r[2]['epoch']=2;r[3]['direction']='英译中';full=compose_history(r);self.assertEqual(len(full['source'].split('\n\n')),3)
  r=[{**r[0],'source':'重复。','translation':'Repeated.'} for _ in range(9)];full=compose_history(r);self.assertEqual(full['source'].count('重复。'),9);self.assertEqual(len(full['source'].split('\n\n')),3)
 def test_empty_and_untrusted_content(self):
  self.assertEqual(compose_history([]),dict(source='',translation=''))
  h=self.fixture();h['records']=[{**h['records'][0],'source':'=SUM(1,2)\n"测试"','translation':'@evil'}];h['count']=1
  data,_=history_file(h,'csv');rows=list(csv.reader(io.StringIO(data.decode('utf-8-sig'))));self.assertTrue(rows[1][2].startswith("'="));self.assertTrue(rows[-1][3].startswith("'@"));self.assertEqual(json.loads(history_file(h,'json')[0])['full_text']['source'],h['records'][0]['source'])
if __name__=='__main__':unittest.main()
