# ping without -I to trigger connect on linux;
# hostname resvolve to A and a AAAA
../../guestbin/ping-once.sh --down east-private.testing.libreswan.org.
# if the connection did not come up start longer one, 30 seeconds or more
../../guestbin/ping-once.sh --up east-private.testing.libreswan.org. || ping -q -c 35 -i 1 east-private.testing.libreswan.org.
ipsec whack --trafficstatus
ipsec _kernel policy -v -E 'type 13[56]'
../../guestbin/ip-xfrm-monitor.sh stop -q
# only 2 acquire messages
grep "acquire proto" OUTPUT/road.ip-xfrm-monitor.txt | wc -l
echo done
