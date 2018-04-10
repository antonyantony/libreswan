ipsec auto --up road
ip link add ipsec0 type xfrm xfrmi-id 6 dev eth0
ip link set ipsec0 up
ip rule add prio 100 to 192.1.2.23/32 not fwmark 7/0xffffffff lookup 50
sleep 2
ip route add table 50 192.1.2.23/32 dev ipsec0 src 192.1.3.209
ping -w 4 -c 4 192.1.2.23
ip -s link show ipsec0
echo done
