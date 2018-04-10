/testing/guestbin/swan-prep
ip addr show dev lo | grep 192.0.3.254 && ip addr del 192.0.3.254/24 dev lo
ip link show ipsec0 2>/dev/null && ip link del ipsec0
echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec whack --impair retransmits
ipsec auto --add road
echo "initdone"
