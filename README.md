# lbe-1420
LBE-1420 GPS locked clock source config

## Prerequisites ##

gcc


## Build Instructions Using GCC ##

For the frequency setting and status utility:
```
    gcc -o lbe-142x lbe-142x.c
```

## Finding Your Device ##

1. List hidraw devices:
```
    ls -l /dev/hidraw*

    crw------- 1 root root 246, 0 12. Aug 09:31 /dev/hidraw0
    crw------- 1 root root 246, 1 22. Aug 16:22 /dev/hidraw1
```

2. Check hidraw devices until you find the correct device
```
    cat /sys/class/hidraw/hidraw1/device/uevent

    DRIVER=hid-generic
    HID_ID=0003:00001DD2:00002443
    HID_NAME=Leo Bodnar Electronics LBE-1420 GPS Locked Clock Source
    HID_PHYS=usb-0000:02:00.0-2.1/input2
    HID_UNIQ=0673ED0E4101
    MODALIAS=hid:b0003g0001v00001DD2p00002443
```
3. Plug the hidraw device back into utility, eg
```
    ./lbe-142x /dev/hidraw1 --f1 10000000 --blink1
```
