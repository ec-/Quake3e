#!/bin/sh

# this can be called from another script as ./q3hostfw <server-id> <port-number>

NAME=${1:-LIMITER} # you need to specify unique table name for each port

PORT=${2:-27960} # and unique server port as well

RATE=768/second
BURST=128

# flush INPUT table:
#iptables -F INPUT

# insert our rule at the beginning of the INPUT chain:
iptables -I INPUT \
    -p udp --dport $PORT -m hashlimit \
    --hashlimit-mode srcip \
    --hashlimit-above $RATE \
    --hashlimit-burst $BURST \
    --hashlimit-name $NAME \
    -j DROP
