/testing/guestbin/swan-prep
ip link show ipsec0 2>/dev/null && ip link del ipsec0
ip route del table 200 192.1.2.23 dev eth0 2>/dev/null
ip route del table 200 default dev eth0 via 192.1.3.254 2>/dev/null
echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec whack --impair retransmits
ipsec auto --add road-east
echo "initdone"
