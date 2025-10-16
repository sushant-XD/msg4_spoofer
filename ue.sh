#!/bin/bash

if [ $EUID -ne 0 ]; then
	echo "Script should be run as root"
	exit 1
fi

set -x

rtue_id=$(docker ps | awk '/ghcr.io\/oran-testing\/rtue/{print $1}')

docker exec $rtue_id bash -c "ip ro add 10.53.0.0/16 via 10.45.0.1"

docker exec -it $rtue_id bash -c "iperf3 -c 10.53.1.1 -u -i 1 -t 36000 -b 2.5M"
