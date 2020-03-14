ipsec whack --impair ignore-soft-expire
ipsec auto --up west
# 21 pings will tigger a soft expire
ping -n -q -c 21 -I 192.0.1.254 192.0.2.254
: ==== cut ====
ip -s xfrm state
: ==== tuc ====
# expect #2 IPsec original Child SA
ipsec trafficstatus
# now trigger hard expire
ping -n -q -c 5 -I 192.0.1.254 192.0.2.254
# #2 will be deleteed and next ping will initiate new Child SA #3
# expect #3 a new Child SA. Rekey will not happen because of impair-soft-expire
sleep 5
ipsec trafficstatus
ping -n -q -c 35 -I 192.0.1.254 192.0.2.254
# expect #4  new IPsec Child SA
sleep 5
ipsec trafficstatus
echo done
