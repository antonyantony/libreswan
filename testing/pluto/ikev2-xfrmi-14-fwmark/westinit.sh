/testing/guestbin/swan-prep
ip link show dev gre1 2>/dev/null >/dev/null && (ip link set down dev gre1 && ip link del gre1)
ip link add dev gre1 type gretap remote 192.1.3.33 local 192.1.2.45 key 123
ip addr add 192.1.7.45/24 dev gre1
ip route add 192.1.3.33 via 192.1.7.33
ip link set gre1 up
../../pluto/bin/wait-until-alive 192.1.7.33
echo "initdone"
: ==== end ====
