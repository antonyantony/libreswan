/testing/guestbin/swan-prep
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec auto --add east
ipsec auto --status | grep east
ipsec whack --impair revival
ipsec whack --impair suppress-retransmits
echo "initdone"
