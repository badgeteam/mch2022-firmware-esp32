#!/usr/bin/env python3

import usb.core
import usb.util

import binascii
import time
import sys
import argparse

parser = argparse.ArgumentParser(description='MCH2022 badge app sideloading tool')
parser.add_argument("name", help="AppFS filename")
parser.add_argument("application", help="Application binary")
parser.add_argument('--run', default=False, action='store_true')
args = parser.parse_args()

name = args.name.encode("ascii")
run = args.run

with open(args.application, "rb") as f:
    application = f.read()

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

print("Installing application \"{}\" ({} bytes)...".format(name.decode("ascii"), len(application)))

print("Connecting...")

while True:
    data = None
    try:
        data = bytes(ep_in.read(32))
    except Exception as e:
        pass
    if (data == b"WUSB"):
        break

packet = b"APOW" + len(name).to_bytes(4, byteorder='little') + name + bytes([(0x01 if run else 0x00)]) + application

ep_out.write(b'WUSB'+len(packet).to_bytes(4, byteorder='little') + binascii.crc32(packet).to_bytes(4, byteorder='little'))

total_size = len(packet)
prev_percent_done = 0

while len(packet) > 0:
    remaining_size = len(packet)
    percent_done = ((total_size - remaining_size) * 100) // total_size
    if prev_percent_done != percent_done:
        print("Writing ({}%)...".format(percent_done))
        prev_percent_done = percent_done
    ep_out.write(packet[:512])
    packet = packet[512:]
    time.sleep(0.01)

print("Writing (100%)...")
print("Installing...")

response = bytes([])

timeout = 100

got_rx_ok = False
got_cmd_init = False
got_cmd_ok = False
got_end = False
got_failed = False
error_memory = False
error_data = False
error_create = False
error_erase = False
error_write = False

while timeout > 0:
    data = None
    try:
        data = bytes(ep_in.read(128))
    except Exception as e:
        pass
    if data != None:
        response += data

    if len(response) >= 4:
        if response[:4] == b"OKOK":
            if not got_rx_ok:
                got_rx_ok = True
            elif got_cmd_init:
                got_cmd_ok = True
        elif response[:4] == b"APOW":
            got_cmd_init = True
        elif response[:4] == b"WUSB":
            got_end = True
            break
        elif response[:4] == b"EMEM":
            got_failed = True
            error_memory = True
        elif response[:4] == b"EDAT":
            got_failed = True
            error_data = True
        elif response[:4] == b"EAPC":
            got_failed = True
            error_create = True
        elif response[:4] == b"EAPE":
            got_failed = True
            error_erase = True
        elif response[:4] == b"EAPW":
            got_failed = True
            error_write = True
        else:
            print("Unknown response data:", response[:4])
        response = response[4:]

    timeout -= 1

if got_failed:
    if error_memory:
        print("Memory allocation failed")
    elif error_data:
        print("No data received")
    elif error_create:
        print("Unable to create file")
    elif error_erase:
        print("Unable to erase flash")
    elif error_write:
        print("Unable to write file")
    else:
        print("Operation failed")
elif got_rx_ok and got_cmd_init and got_cmd_ok and got_end:
    print("Operation completed succesfully")
else:
    print("Something went wrong ({}, {}, {}, {}, {})".format(response, got_rx_ok, got_cmd_init, got_cmd_ok, got_end))

device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_MODE, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0000, webusb_esp32.bInterfaceNumber)

if not run:
    device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_RESET, 0x0000, webusb_esp32.bInterfaceNumber)
