# this an aggressive sanitizer for "ip xfrm state" esp
# careful when mxixing this sanitizer with "ipsec look"
# "ipsec look" sanitizer are similar
# also similar to ip-xfrm.sed, this one leave the port udpencp line.

# this not sanitizing ephemeral data, instead cleaning several of these so
# one can spot relvent bits quickly.
/src 0.0.0.0\/0 dst 0.0.0.0\/0/d
/socket \(in\|out\) priority 0 ptype main/d
/src ::\/0 dst ::\/0/d
/replay-window /d
/auth-trunc hmac/d
/proto esp reqid/d
