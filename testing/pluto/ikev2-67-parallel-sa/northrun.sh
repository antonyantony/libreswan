ipsec auto --status | grep northnet-eastnets
ipsec auto --up northnet-eastnet
taskset 0x1 ping -n -c 2 -I 192.0.3.254 192.0.2.254 
taskset 0x2 ping -n -c 2 -I 192.0.3.254 192.0.2.254
#numactl --physcpubind=0 ping -n -f -I 192.0.3.254 192.0.2.254 &
#numactl --physcpubind=1 ping -n -f -I 192.0.3.254 192.0.2.254 &
#numactl --physcpubind=2 ping -n -f -I 192.0.3.254 192.0.2.254 &
#numactl --physcpubind=3 ping -n -f -I 192.0.3.254 192.0.2.254 &
#numactl --physcpubind=4 ping -n -f -I 192.0.3.254 192.0.2.254 &
ipsec whack --trafficstatus
echo done
