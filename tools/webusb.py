import enum
import usb.core
import usb.util
from enum import Enum
import struct
import time

class Commands(Enum):
    EXECFILE = 0
    HEARTBEAT = 1
    APPFSBOOT = 3
    GETDIR = 4096
    READFILE = 4097
    WRITEFILE = 4098
    DELFILE = 4099
    DUPLFILE = 4100
    MVFILE = 4101
    MAKEDIR = 4102
    APPFSFDIR = 4103
    APPFSDEL = 4104
    APPFSWRITE = 4105

class WebUSBPacket():    
    def __init__(self, command, message_id, payload=None):
        self.command = command
        self.message_id = message_id
        if (payload == None):
            self.payload = b""
        else:
            self.payload = payload

    def getMessage(self):
        return self._generateHeader() + self.payload 

    def _generateHeader(self):
        return struct.pack("<HIHI", self.command.value, len(self.payload), 0xADDE, self.message_id)

class WebUSB():
    def __init__(self):
        self.device = usb.core.find(idVendor=0x16d0, idProduct=0x0f9a)

        if self.device is None:
            raise ValueError("Badge not found")

        self.configuration = self.device.get_active_configuration()

        self.webusb_esp32   = self.configuration[(4,0)]



        self.ep_out = usb.util.find_descriptor(self.webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.ep_in  = usb.util.find_descriptor(self.webusb_esp32, custom_match = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        self.REQUEST_TYPE_CLASS_TO_INTERFACE = 0x21

        self.REQUEST_STATE    = 0x22
        self.REQUEST_RESET    = 0x23
        self.REQUEST_BAUDRATE = 0x24
        self.REQUEST_MODE     = 0x25

        self.TIMEOUT = 15

        self.PAYLOADHEADERLEN = 12

        self.bootWebUSB()
        self.message_id = 1

    def getMessageId(self):
        self.message_id += 1
        return self.message_id

    
    def bootWebUSB(self):
        """
        Boots the badge into webusb mode
        """
        self.device.ctrl_transfer(self.REQUEST_TYPE_CLASS_TO_INTERFACE, self.REQUEST_STATE, 0x0001, self.webusb_esp32.bInterfaceNumber)
        self.device.ctrl_transfer(self.REQUEST_TYPE_CLASS_TO_INTERFACE, self.REQUEST_MODE, 0x0001, self.webusb_esp32.bInterfaceNumber)
        self.device.ctrl_transfer(self.REQUEST_TYPE_CLASS_TO_INTERFACE, self.REQUEST_RESET, 0x0000, self.webusb_esp32.bInterfaceNumber)
        self.device.ctrl_transfer(self.REQUEST_TYPE_CLASS_TO_INTERFACE, self.REQUEST_BAUDRATE, 9216, self.webusb_esp32.bInterfaceNumber)
    
        time.sleep(4)   #Wait for the badge to have boot into webusb mode
        while True:
            try:
                self.ep_in.read(128)
            except Exception as e:
                break

    def receiveResponse(self):
        response = bytes([])
        starttime = time.time()
        while (time.time() - starttime) < self.TIMEOUT:
            data = None
            try:
                data = bytes(self.ep_in.read(128))
            except Exception as e:
                pass
            
            if data != None:
                response += data
            
            if len(response) >= self.PAYLOADHEADERLEN:
                command, payloadlen, verif, message_id = struct.unpack_from("<HIHI", response)
                if verif != 0xADDE:
                    raise Exception("Failed verification")
                if len(response) >= (payloadlen + self.PAYLOADHEADERLEN):
                    return (command, message_id, response[self.PAYLOADHEADERLEN:])
        raise Exception("Timeout in receiving")

    
    def sendPacket(self, packet):
        msg = packet.getMessage()
        starttime = time.time()
        for i in range(0, len(msg), 2048):
            self.ep_out.write(msg[i:i+2048])
            time.sleep(0.01)
            print(f"{i // 2048}/{len(msg) // 2048}")
        #self.ep_out.write(packet.getMessage())
        print(f"transfer speed: {len(msg)/(time.time()-starttime)}")
        command, message_id, data = self.receiveResponse()
        if message_id != packet.message_id:
            raise Exception("Mismatch in id")
        if command != packet.command.value:
            raise Exception("Mismatch in command")
        return data

    def sendHeartbeat(self):
        data = self.sendPacket(WebUSBPacket(Commands.HEARTBEAT, self.getMessageId()))
        return data.decode()

    def getFSDir(self, dir):
        data = self.sendPacket(WebUSBPacket(Commands.GETDIR, self.getMessageId(), dir.encode(encoding='utf-8')))
        data = data.decode()
        data = data.split("\n")
        result = dict()
        result["dir"] = data[0]
        result["files"] = list()
        result["dirs"] = list()
        for i in range(1, len(data)):
            fd = data[i]
            if fd[0] == "f":
                result["files"].append(fd[1:])
            else:
                result["dirs"].append(fd[1:])
        return result

    def appfsUpload(self, appname, file):
        payload = appname.encode(encoding="ascii") + b"\x00" + file
        data = self.sendPacket(WebUSBPacket(Commands.APPFSWRITE, self.getMessageId(), payload))
        return data.decode() == "ok"
    
    def appfsRemove(self, appname):
        payload = appname.encode(encoding="ascii") + b"\x00"
        data = self.sendPacket(WebUSBPacket(Commands.APPFSDEL, self.getMessageId(), payload))
        return data.decode() == "ok"

    def appfsExecute(self, appname):
        payload = appname.encode(encoding="ascii") + b"\x00"
        data = self.sendPacket(WebUSBPacket(Commands.APPFSBOOT, self.getMessageId(), payload))
        return data.decode() == "ok"

    def appfsList(self):
        data = self.sendPacket(WebUSBPacket(Commands.APPFSFDIR, self.getMessageId()))
        print(data)