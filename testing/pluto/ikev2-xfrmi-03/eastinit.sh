/testing/guestbin/swan-prep
ipsec start
/testing/pluto/bin/wait-until-pluto-started
ipsec auto --add north-east-a
ipsec auto --add north-east-b
echo "initdone"
