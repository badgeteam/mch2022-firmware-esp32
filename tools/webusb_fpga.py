#!/usr/bin/env python3

import usb.core
import usb.util
import binascii
import time
import sys
import argparse

REQUEST_TYPE_CLASS_TO_INTERFACE = 0x21 # This is a bitfield, see USB spec (has nothing to do with the other REQUEST_ defines)

REQUEST_STATE    = 0x22 # Defined in webusb_task.c of the RP2040 firmware
REQUEST_RESET    = 0x23 # Defined in webusb_task.c of the RP2040 firmware
REQUEST_BAUDRATE = 0x24 # Defined in webusb_task.c of the RP2040 firmware
REQUEST_MODE     = 0x25 # Defined in webusb_task.c of the RP2040 firmware

parser = argparse.ArgumentParser(description='MCH2022 badge FPGA bitstream programming tool')
parser.add_argument("bitstream", help="Bitstream binary")
args = parser.parse_args()

with open(args.bitstream, "rb") as f:
    bitstream = f.read()

device = usb.core.find(idVendor=0x16d0, idProduct=0x0f9a)

if device is None:
    raise ValueError("Badge not found")

configuration = device.get_active_configuration()

webusb_esp32 = configuration[(4,0)]

esp32_ep_out = usb.util.find_descriptor(webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
esp32_ep_in  = usb.util.find_descriptor(webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0001, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_MODE, 0x0002, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_RESET, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_BAUDRATE, 9216, webusb_esp32.bInterfaceNumber)

print("Waiting for ESP32 to boot into FPGA download mode...")

while True:
    data = None
    try:
        data = bytes(esp32_ep_in.read(4))
    except Exception as e:
        #print(e)
        pass
    if (data == b"FPGA"):
        break

esp32_ep_out.write(b'FPGA' + (len(bitstream).to_bytes(4, byteorder='little')) + binascii.crc32(bitstream).to_bytes(4, byteorder='little'))

print("Loading", end="")

sent = 0
while len(bitstream) - sent > 0:
    print(".", end="")
    sys.stdout.flush()
    txLength = len(bitstream)
    if txLength > 2048:
        txLength = 2048
    esp32_ep_out.write(bitstream[sent:sent + txLength])
    time.sleep(0.05)
    sent += txLength
print("\n")

# Disconnect
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0000, webusb_esp32.bInterfaceNumber)
