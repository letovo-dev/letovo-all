#!/bin/bash
# Author: scv

echo "running auto_installer.py to check all dependencies"

if ! python3 auto_installer.py; then
    echo "auto_installer.py failed. Exiting."
    exit 1
fi

while getopts "aswbh" opt; do
    case ${opt} in
        a )
            param_a=true
            ;;
        s )
            param_s=true
            ;;
        w )
            param_w=true
            ;;
        b )
            param_b=true
            ;;
        --stop)
            stop_services=true
            ;;
        h )
            echo $'\n\nUsage:\n\trun.sh [options]\n\nGeneral Options\n\t[-a] to run with auth server \n\t[-s] to run with secrets server \n\t[-w] to run with wiki server \n\t[-b] to run with iterfase bot for wiki\n\t[--stop] to stop all running servers\n\t[-h] to help\n'
            exit 0  
            ;;
        \? )
            echo "Invalid option: -$OPTARG" 1>&2
            exit 1
            ;;
    esac
done

parent_path=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )
echo "parent path: $parent_path"

current_ip=$(curl -s http://ipv4.icanhazip.com)

echo "Current public IPv4: $current_ip"

export CURRENT_IP=$current_ip


pids=()

if [ "$param_a" = true ]; then
    echo "running auth server"
    ./install-run-core.sh &
    pids+=($!)
    # exit 0
fi

if [ "$param_b" = true ]; then
    echo "running secrets bot"

    "$parent_path/venv/bin/python3" "$parent_path/src/letovo-secrets/src/interfase_bot.py" &
    pids+=($!)
    # exit 0
    echo pids: "${pids[@]}"
fi

if [ "$param_s" = true ]; then
    echo "running secrets server"
    "$parent_path/venv/bin/python3" "$parent_path/src/letovo-secrets/src/server.py" & 
    pids+=($!)
    # exit 0
    echo pids: "${pids[@]}"
fi

if [ "$param_w" = true ]; then
    echo "running wiki server"
    cd "$parent_path/src/letovo-wiki"
    npx docusaurus start &
    pids+=($!)
    # exit 0
    echo pids: "${pids[@]}"
fi

echo "${pids[@]}" > "$parent_path/pids"

if [ "$stop_services" = true ]; then
    echo "stopping all running servers"
    if [ -f "$parent_path/pids" ]; then
        mapfile -t pids < "$parent_path/pids"
    else
        echo "pids file does not exist. Exiting."
        exit 1
    fi
    for pid in "${pids[@]}"; do
        kill $pid
    done
    # exit 0
fi
echo pids: "${pids[@]}"
