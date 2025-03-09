#!/usr/bin/env python3

import sys, appfs

def openAppfs(filename):
    print(filename)
    print("----------------------------------------")
    with open(filename, "rb") as f:
        appfsData = f.read()
    
    obj = appfs.AppFS(appfsData)
    obj.extract_files()

if len(sys.argv) != 2:
    print("Usage: " + sys.argv[0] + " <filename>")
    sys.exit(1)
    
openAppfs(sys.argv[1])
