/testing/guestbin/swan-prep --hostkeys --46
sysctl -w net.core.xfrm_aevent_etime=0xffffffff
sysctl -w net.core.xfrm_aevent_rseqth=0xffffffff
ipsec start
../../guestbin/wait-until-pluto-started
dig +short  @192.1.3.254 road.testing.libreswan.org  IPSECKEY | sort
ipsec _kernel policy -v -E 'type 13[56]'
../../guestbin/ip-xfrm-monitor.sh start
echo "initdone"
