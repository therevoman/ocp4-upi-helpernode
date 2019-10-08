pkill -9 ErnicImitator
pkill -9 ImageReceiver
make clean
make
sudo ifconfig enp175s0 mtu 4200
./ImageReceiver 54321 1 1
