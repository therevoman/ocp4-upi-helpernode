#!/bin/bash
#make clean
#make
#sudo ifconfig enp175s0 mtu 4200
echo "Output of ip information"
echo ""
ip a
echo ""
echo ""
echo "Beginning to run /app/ImageReceiver 54321 1 1"

/app/ImageReceiver 54321 1 1
