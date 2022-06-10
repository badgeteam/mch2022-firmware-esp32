#!/usr/bin/env python3

import usb.core
import usb.util

import binascii
import time
import sys
import argparse

parser = argparse.ArgumentParser(description='MCH2022 badge app remove tool')
parser.add_argument("name", help="AppFS filename")
args = parser.parse_args()

name = args.name.encode("ascii")

device = usb.core.find(idVendor=0x16d0, idProduct=0x0f9a)

if device is None:
    raise ValueError("Badge not found")

configuration = device.get_active_configuration()

webusb_esp32   = configuration[(4,0)]

ep_out = usb.util.find_descriptor(webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
ep_in  = usb.util.find_descriptor(webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

REQUEST_TYPE_CLASS_TO_INTERFACE = 0x21

REQUEST_STATE    = 0x22
REQUEST_RESET    = 0x23
REQUEST_BAUDRATE = 0x24
REQUEST_MODE     = 0x25

device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0001, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_MODE, 0x0001, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_RESET, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_BAUDRATE, 9216, webusb_esp32.bInterfaceNumber)

print("Directory listing for \"{}\"...".format(name.decode("ascii")))

print("Connecting...")

while True:
    data = None
    try:
        data = bytes(ep_in.read(4))
    except Exception as e:
        pass
    if (data == b"WUSB"):
        break

packet = b"FSLS" + name + bytes([0])
ep_out.write(b'WUSB'+len(packet).to_bytes(4, byteorder='little') + binascii.crc32(packet).to_bytes(4, byteorder='little'))

print("Writing...")

while len(packet) > 0:
    ep_out.write(packet[:512])
    packet = packet[512:]
    time.sleep(0.01)

response = bytes([])

timeout = 100

got_rx_ok = False
got_cmd_init = False
got_cmd_ok = False
got_end = False
got_failed = False

in_file = False
in_dir = False
name_length = 0

files = []
directories = []

print("Reading...")

while timeout > 0:
    data = None
    try:
        data = bytes(ep_in.read(64))
    except Exception as e:
        pass
    if data != None:
        response += data

    if in_file:
        if len(response) < (12 + name_length):
            pass # Receiving data
        elif name_length == 0 and len(response) >= 4:
            name_length = int.from_bytes(response[:4], "little")
        else:
            name = response[4:4+name_length].decode("ascii")
            size = int.from_bytes(response[4+name_length:8+name_length], "little")
            time = int.from_bytes(response[8+name_length:12+name_length], "little")
            response = response[12+name_length:]
            files.append({
                "name": name,
                "size": size,
                "time": time
            })
            in_file = False
            name_length = 0
    elif in_dir:
        if len(response) < (12 + name_length):
            pass # Receiving data
        elif name_length == 0 and len(response) >= 4:
            name_length = int.from_bytes(response[:4], "little")
        else:
            name = response[4:4+name_length].decode("ascii")
            size = int.from_bytes(response[4+name_length:8+name_length], "little")
            time = int.from_bytes(response[8+name_length:12+name_length], "little")
            response = response[12+name_length:]
            directories.append({
                "name": name,
                "size": size,
                "time": time
            })
            in_dir = False
            name_length = 0
        pass
    elif len(response) >= 4:
        if response[:4] == b"OKOK":
            if not got_rx_ok:
                got_rx_ok = True
            elif got_cmd_init:
                got_cmd_ok = True
        elif response[:4] == b"FSLS":
            got_cmd_init = True
        elif response[:4] == b"WUSB":
            got_end = True
            break
        elif response[:4] == b"ESTR":
            got_failed = True
        elif response[:4] == b"FAIL":
            got_failed = True
        elif response[:4] == b"FILE":
            in_file = True
        elif response[:4] == b"DIRR":
            in_dir = True
        else:
            print("Unknown response data:", response[:4])
        response = response[4:]

    timeout -= 1

if got_failed:
    print("Operation failed")
elif got_rx_ok and got_cmd_init and got_cmd_ok and got_end:
    print("Operation completed succesfully")
    for d in directories:
        print("Directory \"{}\"".format(d["name"]))
    for f in files:
        print("File \"{}\" {}KB".format(f["name"], f["size"] // 1024))
else:
    print("Something went wrong ({}, {}, {}, {}, {})".format(response, got_rx_ok, got_cmd_init, got_cmd_ok, got_end))

device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_MODE, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_RESET, 0x0000, webusb_esp32.bInterfaceNumber)
