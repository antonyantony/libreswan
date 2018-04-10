ipsec auto --up road-east
ip link add ipsec0 type xfrm xfrmi-id 0 dev eth0
ip link set ipsec0 up
ip rule add  prio 100 not fwmark 7/0xffffffff lookup 50
ip route add table 50 192.1.2.23/32 dev ipsec0
ping -w 4 -c 4 192.1.2.23
ipsec whack --trafficstatus
echo done
