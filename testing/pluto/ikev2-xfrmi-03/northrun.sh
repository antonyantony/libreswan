ipsec auto --up north-a
ipsec auto --up north-b
ip link add ipsec0 type xfrm xfrmi-id 6 dev eth0
ip link set ipsec0 up
# tcpdump -s 0 -n -w /tmp/ipsec0.pcap -i ipsec0 &
# echo $! > OUTPUT/tcpdump.pid
ip rule add prio 100 to 192.1.2.23/32 not fwmark 7/0xffffffff lookup 50
sleep  2
ip route add table 50 192.1.2.23/32 dev ipsec0
ping -w 4 -c 4 -I  192.1.3.33 192.1.2.23
ping -w 4 -c 4 -I  192.1.3.34 192.1.2.23
ip -s link show ipsec0
# kill -9 $(cat OUTPUT/tcpdump.pid)
sleep 2
# cp /tmp/ipsec0.pcap OUTPUT/
echo done
