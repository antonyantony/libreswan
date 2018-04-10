ipsec auto --up north
# comments bellow are to understand/explore the basics : what is going on
# ip link add ipsec0 type xfrm xfrmi-id 1 dev eth0
# ip link set ipsec0 up
# ip route add 192.0.2.0/24 dev ipsec0 src 192.0.3.254
# tcpdump -s 0 -n -w /tmp/ipsec0.pcap -i ipsec0 & echo $! > /tmp/tcpdump.pid
sleep  2
ping -w 4 -c 4 192.0.2.254
ip -s link show ipsec0
#kill -9 $(cat /tmp/tcpdump.pid)
sleep 2
#cp /tmp/ipsec0.pcap OUTPUT/
ip rule show
ip route show table 50
echo done
