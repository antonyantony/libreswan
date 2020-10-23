#!/bin/sh

ip=$(ip address show dev eth0 | awk '$1 == "inet" { print gensub("/[0-9]*", "", 1, $2)}')
echo ip: ${ip}

hostname=$(awk '$1 == IP { host=$2; } END { print host; }' IP=${ip} /etc/hosts)
echo hostname: ${hostname}

hostnamectl set-hostname ${hostname}
