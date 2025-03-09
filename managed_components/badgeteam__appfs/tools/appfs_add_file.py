#!/usr/bin/env python3

import sys, appfs

if __name__ == "__main__":
    if len(sys.argv) != 6:
        print("Usage: " + sys.argv[0] + " <appfs binary> <app binary> <name> <title> <version>")
        sys.exit(1)
    
    with open(sys.argv[1], "rb") as f:
        fs = appfs.AppFS(f.read())
        
    with open(sys.argv[2], "rb") as f:
        data = f.read()
        fs.create_file(sys.argv[3], sys.argv[4], int(sys.argv[5]), data)
    
    metadata = fs.get_metadata()
    print("AppFS size is", metadata.get_size() // 1024, "KB,", metadata.get_free() // 1024, "KB free (",(metadata.get_free() * 100 // metadata.get_size()),"%)")
    #metadata.print_usage()
    
    with open(sys.argv[1], "wb") as f:
        f.write(fs.get_data())
