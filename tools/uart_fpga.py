#!/usr/bin/env python3

import binascii, serial, time, sys, argparse


def uart_tx(title, data):

    print(f"{title:s} : ", end="", file=sys.stderr)

    sent = 0
    time.sleep(0.1)
    while len(data) - sent > 0:
        print(".", end="", file=sys.stderr)
        sys.stderr.flush()
        txLength = min(2048, len(data) - sent)
        port.write(data[sent:sent + txLength])
        sent += txLength

    print("", file=sys.stderr)


parser = argparse.ArgumentParser(description='MCH2022 badge FPGA bitstream programming tool')
parser.add_argument("port", help="Serial port")
parser.add_argument("bitstream", help="Bitstream binary")
parser.add_argument("bindings", nargs="*", help="Data files/bindings")
args = parser.parse_args()

# Open UART
port = serial.Serial(args.port, 921600, timeout=0.1)

# Sync
print("Waiting for badge...", file=sys.stderr)
while True:
    data = port.read(128)
    if (data == b"FPGA"):
        break

# Send sync word
port.write(b'FPGA')
time.sleep(0.5)

# Send the data bindings if any
for binfo in args.bindings:
    # Clear ?
    if binfo[0] == '-':
        fid = int(binfo[1:], 0)
        title = f"Clearing FID 0x{fid:08x}"
        packet = [
            b'C',
            fid.to_bytes(4, byteorder='little'),
            b'\x00\x00\x00\x00\x00\x00\x00\x00',
        ]

    # Remote File binding ?
    elif binfo[0] == '=':
        fid, path = binfo[1:].split(':',2)
        fid = int(fid, 0)
        title = f"Binding FID 0x{fid:08x} to path '{path:s}'"
        path = path.encode('utf-8')
        packet = [
            b'F',
            fid.to_bytes(4, byteorder='little'),
            len(path).to_bytes(4, byteorder='little'),
            binascii.crc32(path).to_bytes(4, byteorder='little'),
            path,
        ]

    # Data bindings ? (Local file)
    else:
        fid, path = binfo[0:].split(':',2)
        fid = int(fid, 0)
        title = f"Sending data block for FID 0x{fid:08x} (local file '{path:s}')"
        with open(path, "rb") as fh:
            data = fh.read()
        packet = [
            b'D',
            fid.to_bytes(4, byteorder='little'),
            len(data).to_bytes(4, byteorder='little'),
            binascii.crc32(data).to_bytes(4, byteorder='little'),
            data,
        ]

    # Send it
    uart_tx(title, b''.join(packet))

# Send the bitstream itself
with open(args.bitstream, "rb") as fh:
    bitstream = fh.read()

    bitstream_packet = [
        b'B\x00\x00\x00\x00',
        len(bitstream).to_bytes(4, byteorder='little'),
        binascii.crc32(bitstream).to_bytes(4, byteorder='little'),
        bitstream,
    ]

    uart_tx("Sending bitstream", b''.join(bitstream_packet))


# Print messages
while port.is_open:
    inLen = port.in_waiting
    if inLen > 0:
        data = port.read(inLen).decode('utf-8','ignore');
        if data != 'FPGA':
            print(data, end='')
            sys.stdout.flush()
    else:
        time.sleep(0.01)

port.close()
print("\n")
