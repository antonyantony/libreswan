/testing/guestbin/swan-prep --nokeys
ipsec start
../../guestbin/wait-until-pluto-started
ipsec add westnet-eastnet-clones
ipsec connectionstatus westnet-eastnet-clones
echo "initdone"
