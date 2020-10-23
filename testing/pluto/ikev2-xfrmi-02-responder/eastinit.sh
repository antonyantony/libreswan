/testing/guestbin/swan-prep
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec auto --add east
ipsec whack --impair revival
../bin/tcpdump.sh --start -i eth1
echo "initdone"
