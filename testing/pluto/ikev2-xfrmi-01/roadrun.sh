ipsec auto --up road
ip addr show dev lo | grep 192.0.3.254 || ip addr add 192.0.3.254/24 dev lo
ip link add ipsec0 type xfrm xfrmi-id 6 dev eth0
ip link set ipsec0 up
ip rule add prio 100 to 192.0.2.0/24 not fwmark 7/0xffffffff lookup 50
sleep  2
ip route add table 50 192.0.2.0/24 dev ipsec0 src 192.0.3.254
ping -w 4 -c 4 192.0.2.254
ipsec whack --trafficstatus
ip -s link show ipsec0
echo done
