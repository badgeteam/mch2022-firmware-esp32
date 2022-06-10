#!/usr/bin/env python3

import binascii, serial, time, sys, argparse

parser = argparse.ArgumentParser(description='MCH2022 badge FPGA bitstream programming tool')
parser.add_argument("port", help="Serial port")
parser.add_argument("bitstream", help="Bitstream binary")
args = parser.parse_args()

with open(args.bitstream, "rb") as f:
    bitstream = f.read()

port = serial.Serial(args.port, 921600, timeout=1)
print("Waiting for badge...")
while True:
    data = port.read(4)
    if (data == b"FPGA"):
        break
port.write(b'FPGA' + (len(bitstream).to_bytes(4, byteorder='little')) + binascii.crc32(bitstream).to_bytes(4, byteorder='little'))
time.sleep(0.5)
sent = 0
print("Sending data", end="")
while len(bitstream) - sent > 0:
    print(".", end="")
    sys.stdout.flush()
    txLength = len(bitstream)
    if txLength > 2048:
        txLength = 2048
    port.write(bitstream[sent:sent + txLength])
    time.sleep(0.05)
    sent += txLength
while port.is_open:
    inLen = port.in_waiting
    if inLen > 0:
        data = port.read(inLen).decode('utf-8','ignore');
        if data == 'FPGA':
            print(data)
        else:
            print(data, end='')
port.close()
print("\n")
