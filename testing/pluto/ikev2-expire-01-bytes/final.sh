../../guestbin/ipsec-look.sh
# should match on west
grep "initiating rekey to replace Child SA" OUTPUT/$(hostname).pluto.log
# should be absent
grep "initiating Child SA using IKE SA" OUTPUT/$(hostname).pluto.log || echo "success"
