../../guestbin/ipsec-look.sh
# should match only on west, exactly twice
grep "initiating rekey to replace Child SA" OUTPUT/$(hostname).pluto.log
# should be absent
grep "initiating Child SA using IKE SA" OUTPUT/$(hostname).pluto.log || echo "success"
