#!/usr/bin/env python3

import usb.core
import usb.util

import binascii, time, sys, argparse

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

print("Sync...")
while True:
    data = None
    try:
        data = bytes(ep_in.read(32))
    except Exception as e:
        pass
    if (data == b"WUSB"):
        break

packet = b"APLS"
ep_out.write(b'WUSB'+len(packet).to_bytes(4, byteorder='little') + binascii.crc32(packet).to_bytes(4, byteorder='little') + packet)

response = bytes([])
while True:
    data = None
    try:
        data = bytes(ep_in.read(128))
    except Exception as e:
        pass
    if data == b"WUSB":
        break
    if data != None:
        response += data

def parseResponse(response):
    if (response[:4] != b"OKOK"):
        print("Failure: not OK")
        return
    response = response[4:]
    if (response[:4] != b"APLS"):
        print("Failure: not LS")
        return
    response = response[4:]
    amount_of_files = int.from_bytes(response[0:4], 'little')
    response = response[4:]
    buffer_size = int.from_bytes(response[0:4], 'little')
    response = response[4:]
    print("Amount of files:", amount_of_files)
    
    print("{0: <5}  {1:}".format("size", "name"))
    print("==============================")
    for i in range(amount_of_files):
        appfs_fd = int.from_bytes(response[0:4], 'little')
        response = response[4:]
        name_length = int.from_bytes(response[0:4], 'little')
        response = response[4:]
        name = response[:name_length]
        response = response[name_length:]
        print("{0: <5}  \"{1:}\"".format(appfs_fd, name.decode("ascii")))
    

parseResponse(response)

device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_MODE, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_RESET, 0x0000, webusb_esp32.bInterfaceNumber)
device.ctrl_transfer(REQUEST_TYPE_CLASS_TO_INTERFACE, REQUEST_STATE, 0x0000, webusb_esp32.bInterfaceNumber)
