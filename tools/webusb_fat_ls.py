#!/usr/bin/env python3
from webusb import *

import binascii
import time
import sys
import argparse

parser = argparse.ArgumentParser(description='MCH2022 badge app remove tool')
parser.add_argument("name", help="AppFS filename")
args = parser.parse_args()

name = args.name
dev = WebUSB()
res = dev.getFSDir(name)
print("Directory listing for \"{}\"...".format(name))
for d in res["dirs"]:
    print("Directory \"{}\"".format(d))
for f in res["files"]:
    print("File \"{}\"".format(f))
