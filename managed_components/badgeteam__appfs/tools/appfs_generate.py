#!/usr/bin/env python3

import sys, appfs

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: " + sys.argv[0] + " <size in bytes> <filename>")
        sys.exit(1)
    
    fs = appfs.AppFS(int(sys.argv[1]))
    
    metadata = fs.get_metadata()
    print("AppFS created, size is", metadata.get_size() // 1024, "KB")
    
    with open(sys.argv[2], "wb") as f:
        f.write(fs.get_data())
