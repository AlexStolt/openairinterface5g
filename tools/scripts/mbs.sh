#!/bin/bash
# SPDX-License-Identifier: LicenseRef-CSSL-1.0
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OAI_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
SCRIPT=$(basename "$0")

CONF_DIR=$OAI_DIR/ci-scripts/conf_files
UE_CONF=$CONF_DIR/mbs-ue.yaml
GNB_CONF=$CONF_DIR/mbs-gnb.yaml

CN=$OAI_DIR/ci-scripts/yaml_files/5g_rfsimulator

OAI=$OAI_DIR/cmake_targets
GNB=$OAI/ran_build/build/nr-softmodem
UE=$OAI/ran_build/build/nr-uesoftmodem

MULTI_UE=$SCRIPT_DIR/multi-ue.sh

TIME=20
IMSI=208990100001099


if [ "$1" = "help" ]; then

    echo "1. Deploy CN: ./$SCRIPT cn"
    echo "-------------------------------------------------------------"
    echo "2. Deploy gNB with HARQ: ./$SCRIPT nr"
    echo "2. Deploy gNB without HARQ: ./$SCRIPT nr noharq"
    echo "-------------------------------------------------------------"
    echo "3. Deploy the first UE: ./$SCRIPT ue 1"
    echo "4. Deploy the another UE with noise: ./$SCRIPT ue 2 -44"
    echo "-------------------------------------------------------------"
    echo "5. Listen for the MBS UDP traffic in UE N: ./$SCRIPT ll <N>"
    echo "-------------------------------------------------------------"
    echo "6. Send UDP from DN: ./$SCRIPT dl <M>"


elif [ "$1" = "cn" ]; then
    docker pull mysql:9.6
    docker pull oaisoftwarealliance/oai-amf:v2.2.1
    docker pull oaisoftwarealliance/oai-smf:v2.2.1
    docker pull oaisoftwarealliance/oai-upf:v2.2.1
    docker pull oaisoftwarealliance/trf-gen-cn5g:latest

    docker container prune
    sudo service docker restart

    (cd "$CN" && docker compose up -d mysql oai-amf oai-smf oai-upf oai-ext-dn)


elif [ "$1" = "nr" ]; then
    (cd "$OAI" && ./build_oai --gNB --nrUE --ninja -w SIMU) || exit 1

    if [ "$2" = "noharq" ]; then
        sudo "$GNB" -O "$GNB_CONF" -E --rfsim --MBS_TEST.[0].harq_feedback 0
    else
        sudo "$GNB" -O "$GNB_CONF" -E --rfsim
    fi


elif [ "$1" = "ue" ]; then
    sudo "$MULTI_UE" -d$2 >/dev/null 2>&1
    while sudo iptables -t nat -D POSTROUTING -s 10.$((200 + $2)).1.0/24 -o lo -j MASQUERADE 2>/dev/null; do :; done
    while sudo iptables -D FORWARD -i lo -o v-eth$2 -j ACCEPT 2>/dev/null; do :; done
    while sudo iptables -D FORWARD -o lo -i v-eth$2 -j ACCEPT 2>/dev/null; do :; done
    sudo "$MULTI_UE" -c$2 || exit 1

    if [[ -z "$3" ]]; then
        sudo ip netns exec ue$2 "$UE" -C 3319680000 -r 106 --numerology 1 --ssb 516 -E --rfsim -O "$UE_CONF" \
            --uicc0.imsi $(($IMSI + $2)) --rfsimulator.[0].serveraddr 10.$((200 + $2)).1.100
    else
        sudo ip netns exec ue$2 "$UE" -C 3319680000 -r 106 --numerology 1 --ssb 516 -E --rfsim -O "$UE_CONF" \
            --uicc0.imsi $(($IMSI + $2)) --rfsimulator.[0].serveraddr 10.$((200 + $2)).1.100 \
            --rfsimulator.[0].options chanmod --channelmod.noise_power_dBFS $3
    fi


elif [ "$1" = "ll" ]; then
    IP=$(sudo ip netns exec ue1 ifconfig oaitun_ue1 | grep 'inet ' | awk '{print $2}')

    if [ "$2" != "1" ]; then
        sudo ip netns exec ue$2 ifconfig oaitun_ue1 $IP netmask 255.255.255.0
        sudo ip netns exec ue$2 ip route replace default dev oaitun_ue1
    fi

    sudo ip netns exec ue$2 iperf -su -B $IP -i 1 -l 1000


elif [ "$1" = "dl" ]; then
    IP=$(sudo ip netns exec ue1 ifconfig oaitun_ue1 | grep 'inet ' | awk '{print $2}')
    docker exec -it rfsim5g-oai-ext-dn iperf -u -c $IP -i 1 -b "$2"M -t $TIME -l 1000
fi
