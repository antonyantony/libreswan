# trigger OE
../../pluto/bin/ping-once.sh --down -I 192.1.3.209 192.1.2.23
../../pluto/bin/wait-for.sh --match '"private-or-clear#192.1.2.0/24"' -- ipsec whack --trafficstatus
../../pluto/bin/ping-once.sh --up   -I 192.1.3.209 192.1.2.23
# try shunt
../../pluto/bin/ping-once.sh --down -I 192.1.3.209 7.7.7.7
ipsec whack --shuntstatus
# wait on OE retransmits and rekeying - shuntstatus is empty because
# shunt 7.7.7.7 is not bare and its conn negotiationshunt=hold, so
# ping should fail
sleep 3
ipsec whack --shuntstatus
ipsec whack --trafficstatus
../../pluto/bin/ping-once.sh --down -I 192.1.3.209 7.7.7.7
# let acquire time out; shunt is now failureshunt=pass and should show
# up, ping will work
../../pluto/bin/wait-for.sh --timeout 60 --match 7.7.7.7 -- ipsec whack --shuntstatus
../../pluto/bin/ping-once.sh --up -I 192.1.3.209 7.7.7.7
../../pluto/bin/ipsec-look.sh
# let failureshunt expire - both from bare shunt list as as kernel policy
# XXX: what is below looking for?  Output doesn't seem to change.
sleep 60
../../pluto/bin/ipsec-look.sh
ipsec whack --shuntstatus
sleep 60
../../pluto/bin/ipsec-look.sh
ipsec whack --shuntstatus
sleep 60
../../pluto/bin/ipsec-look.sh
ipsec whack --shuntstatus
killall ip > /dev/null 2> /dev/null
cp /tmp/xfrm-monitor.out OUTPUT/road.xfrm-monitor.txt
echo done
