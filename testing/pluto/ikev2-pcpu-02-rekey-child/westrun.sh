ipsec up westnet-eastnet-clones
# wait for all 4 Additional Child SAs (RFC 9611, clones=4) plus the
# Initial Child SA to establish before checking anything below -
# Additional Child SAs are created via separate, async CREATE_CHILD_SA
# exchanges after the Initial SA is up
../../guestbin/wait-for.sh --timeout 20 --match '^5$' -- sh -c 'ipsec trafficstatus | wc -l'
../../guestbin/ping-once.sh --up -I 192.0.1.254 192.0.2.254
ipsec trafficstatus
ipsec trafficstatus | wc -l
ipsec _kernel state | grep -c '^src'
ipsec _kernel state | grep pcpu-num | sort -u | wc -l
# rekey CHILD SA by connection name; this rekeys whichever Child SA is
# .established_child_sa for this connection - the last Additional Child
# SA to establish
ipsec whack --rekey-child --name westnet-eastnet-clones
../../guestbin/wait-for.sh --timeout 20 --match '^5$' -- sh -c 'ipsec trafficstatus | wc -l'
../../guestbin/ping-once.sh --up -I 192.0.1.254 192.0.2.254
ipsec trafficstatus
ipsec trafficstatus | wc -l
ipsec _kernel state | grep -c '^src'
ipsec _kernel state | grep pcpu-num | sort -u | wc -l
echo done
