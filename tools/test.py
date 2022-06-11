from time import sleep
from webusb import *
dev = WebUSB()
print(dev.getFSDir("/sdcard"))
print(dev.getFSDir("/flash"))
dev.appfsList()
file = open("bme680.bin", "rb")
app = file.read()
file.close()
dev.appfsUpload("badgePython", app)
dev.appfsList()
res = dev.appfsList()
print(res)