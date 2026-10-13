#!/usr/bin/env python3
"""Fetch only the S3 vendor blobs declared by the pinned HAL; verify SHA-256."""
import argparse
import hashlib
from pathlib import Path
import urllib.request
import yaml

parser=argparse.ArgumentParser()
parser.add_argument("hal",type=Path)
args=parser.parse_args()
for blob in yaml.safe_load((args.hal/"zephyr/module.yml").read_text())["blobs"]:
    if not blob["path"].startswith("lib/esp32s3/"):continue
    dest=args.hal/"zephyr/blobs"/blob["path"]
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest()==blob["sha256"]:
        continue
    url=blob["url"].replace("https://github.com/","https://raw.githubusercontent.com/").replace("/raw/","/")
    with urllib.request.urlopen(url,timeout=60) as response:data=response.read()
    if hashlib.sha256(data).hexdigest()!=blob["sha256"]:raise ValueError(f"Digest mismatch: {dest}")
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_bytes(data)
    print("Verified",blob["path"])
