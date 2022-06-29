#!/usr/bin/env python3
from webusb import *
import argparse

parser = argparse.ArgumentParser(description='MCH2022 badge FAT FS upload tool')
parser.add_argument("name", help="filename local")
parser.add_argument("target", help="filename local")
args = parser.parse_args()

name = args.name
dev = WebUSB()
with open(args.name, "rb") as file:
    data = file.read()
res = dev.pushFSfile(args.target, data)
if res:
    print("File uploaded")
else:
    print("Uploaded failed")
