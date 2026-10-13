import importlib.util,tempfile,hashlib
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/read-logs.py'
spec=importlib.util.spec_from_file_location('readlogs',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Serial:
 def __init__(self):self.lines=[];self.reads=0
 def reset_input_buffer(self):self.lines=[]
 def write(self,cmd):
  words=cmd.decode().split()
  if words[1]=='next':self.lines=[b'adv> echo\n',b'@ADVLOG FILE 0 3\n'];return
  self.reads+=1
  # A corrupt frame and a stale offset must not be accepted; followed by valid data.
  self.lines=[b'@ADVLOG DATA 0 0 3 3 00000000 616263\n',b'@ADVLOG DATA 0 1 4 3 1a47e90b 616263\n',b'@ADVLOG DATA 0 0 3 3 1a47e90b 616263\n']
 def readline(self):return self.lines.pop(0) if self.lines else b''
client=m.LogClient(Serial());assert client.next_file(0)==(0,3)
with tempfile.TemporaryDirectory() as d:
 path=Path(d)/'log.txt';assert client.download(0,3,path)==hashlib.sha256(b'abc').hexdigest();assert path.read_bytes()==b'abc'
print('PASS: export client rejects corrupted/stale frames; preserves exact bytes and verifies file hash')

class TimeoutOnce(Serial):
 def __init__(self):super().__init__();self.attempts=0
 def write(self,cmd):
  self.attempts+=1
  if self.attempts==1:self.lines=[b'@ADVLOG ERROR -116\n']
  else:super().write(cmd)
port=TimeoutOnce();assert m.LogClient(port).next_file(0)==(0,3) and port.attempts==2
print('PASS: Zephyr/picolibc timeout code is retried')
