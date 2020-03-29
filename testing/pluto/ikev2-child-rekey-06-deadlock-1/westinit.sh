/testing/guestbin/swan-prep
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec whack --impair revival
ipsec whack --impair suppress-retransmits
ipsec auto --add west
# Delay outgoing the second IPsec rekey message, which has IKE Message ID: 4
# Message ID : 0 = IKE_INIT, 1 = IKE_AUTH, 2 = REKEY (First one let it go)
# 3 : DELETE, 4 = REKEY (DROP)
tc qdisc del dev eth0 root # Ensure you start from a clean slate
tc qdisc add dev eth0 root handle 1: prio
tc qdisc add dev eth0 parent 1:3 handle 30: netem delay 5000ms
# tc filter add dev ppp14 parent 1:0 prio 10 u32 match u8 64 0xff at 8 flowid 1:4
tc filter add dev eth0 protocol ip parent 1:0 prio 3 u32 match u8 4 at 0x41 flowid 1:3
sleep 4
echo "initdone"
