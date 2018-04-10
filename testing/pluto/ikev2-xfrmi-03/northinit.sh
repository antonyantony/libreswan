/testing/guestbin/swan-prep
ip addr show dev eth0 | grep 192.1.3.34 || ip addr add 192.1.3.34/24 dev eth0
ip link show ipsec0 2>/dev/null && ip link del ipsec0
echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec whack --impair retransmits
ipsec auto --add north-a
ipsec auto --add north-b
echo "initdone"
