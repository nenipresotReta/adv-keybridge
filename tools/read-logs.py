#!/usr/bin/env python3
"""Read-only, retryable SD log download. Requires pyserial; never flashes/resets."""
import argparse, hashlib, json, re, time
from pathlib import Path

def checksum(data):
    result=2166136261
    for byte in data: result=((result^byte)*16777619)&0xffffffff
    return result

class LogClient:
    def __init__(self, serial): self.serial=serial
    def command(self, command, expected, attempts=4):
        for attempt in range(attempts):
            self.serial.reset_input_buffer()
            self.serial.write((command+'\r').encode())
            end=time.monotonic()+5
            while time.monotonic()<end:
                raw=self.serial.readline().decode('ascii',errors='replace')
                line=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',raw)
                pos=line.find('@ADVLOG ')
                if pos<0: continue
                fields=line[pos:].strip().split()
                if fields[:2]==['@ADVLOG','ERROR']:
                    error=int(fields[2])
                    if error==-2: return None
                    # Busy, late reply, reset/remount and timing failures can be retried.
                    if error in (-16,-19,-110,-116): break  # Zephyr/picolibc ETIMEDOUT is 116.
                    raise RuntimeError('Device log error: '+str(error))
                if expected(fields): return fields
            time.sleep(.2)
        raise RuntimeError('No valid response to '+command+'; keep Host off and check SD LOG ON')
    def next_file(self, index):
        def valid(f):
            return len(f)==4 and f[:2]==['@ADVLOG','FILE'] and f[2].isdigit() and f[3].isdigit() and index<=int(f[2])<128 and int(f[3])<=1048576
        fields=self.command('advlog next '+str(index),valid)
        return None if fields is None else (int(fields[2]),int(fields[3]))
    def read(self,index,offset):
        def valid(f):
            try:
                if len(f) not in (7,8) or f[:2]!=['@ADVLOG','DATA']: return False
                if int(f[2])!=index or int(f[3])!=offset: return False
                data=bytes.fromhex(f[7]) if len(f)==8 else b''
                return len(data)==int(f[5])<=128 and checksum(data)==int(f[6],16) and offset+len(data)<=int(f[4])<=1048576
            except (ValueError,IndexError): return False
        fields=self.command(f'advlog read {index} {offset}',valid)
        if fields is None: raise RuntimeError('Log disappeared during export')
        return bytes.fromhex(fields[7]) if len(fields)==8 else b''
    def download(self,index,size,path):
        # A fixed initial size avoids chasing a log that is still growing.
        with path.open('xb') as out:
            offset=0
            while offset<size:
                data=self.read(index,offset)[:size-offset]
                if not data: raise RuntimeError('Unexpected end of log')
                out.write(data);offset+=len(data)
        return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    import serial
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--file',type=int,help='Only download this log number')
    args=parser.parse_args()
    if args.file is not None and not 0<=args.file<128: parser.error('--file must be 0..127')
    ports=list(Path('/dev').glob('cu.usbmodem*'))
    port=args.port or (str(ports[0]) if len(ports)==1 else None)
    if not port: parser.error('Specify --port; expected one ADV USB serial port')
    args.output.mkdir(parents=True,exist_ok=False)
    connection=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=2)
    connection.dtr=False;connection.rts=False;connection.port=port
    manifest={}
    try:
        connection.open();client=LogClient(connection)
        index=args.file if args.file is not None else 0
        while index<128:
            item=client.next_file(index)
            if item is None: break
            number,size=item
            if args.file is not None and number!=args.file: raise RuntimeError('Requested file not found')
            path=args.output/f'LOG{number:05d}.TXT'
            print(f'Reading {path.name} ({size} bytes)',flush=True)
            sha=client.download(number,size,path)
            manifest[path.name]={'bytes':size,'sha256':sha}
            if args.file is not None: break
            index=number+1
        (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(f'Saved {len(manifest)} logs to {args.output}')
    finally: connection.close()
if __name__=='__main__': main()
