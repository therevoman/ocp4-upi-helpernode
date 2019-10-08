pkill -9 ErnicImitator
pkill -9 ImageReceiver
make clean
make
sudo ifconfig enp175s0 mtu 4200
./ErnicImitator 0 1 54321 10.1.0.16
