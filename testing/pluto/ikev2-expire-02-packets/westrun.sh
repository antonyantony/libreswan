ipsec auto --up west
# 10 pings will rekey
ping -n -q -c 8 -I 192.0.1.254 192.0.2.254
# expect #2 IPsec original Child SA
ipsec trafficstatus
# next pings will go over and initiate a rekey
ping -n -q -c 3 -I 192.0.1.254 192.0.2.254
sleep 5
# expect #3 IPsec first rekeyed Child SA
ipsec trafficstatus
ping -n -q -c 11 -I 192.0.1.254 192.0.2.254
sleep 5
# expect #4 IPsec second rekeyed Child SA
ipsec trafficstatus
echo done
