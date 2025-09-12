#!/bin/bash 

test_file="test.cpp"
work_file="server.cpp"
build_files=""
checks_flag=true
run_flag=true
logging=true
generate=false
generate_full=false
generate_ai=false
docker_image=false
# Read and parse BuildConfig.json
config_file="./BuildConfig.json"

python3 action-maker.py

if [ -f "$config_file" ]; then
    test_file=$(jq -r '.test_file' "$config_file")
    work_file=$(jq -r '.work_file' "$config_file")
    for i in $(jq -c '.build_files[]' "$config_file"); do
        build_files+="$i "
    done 
    build_files=$(echo "$build_files" | tr -d '"')
    build_files=$(echo "$build_files" | tr '\n' ' ')
else
    echo "Config file not found!"
    exit 1
fi

echo "test file $test_file"
echo "work file $work_file"
echo "build files: $build_files"

while getopts 'dgptoifdhas:' OPTION; do
    case "$OPTION" in
        d)
            echo "building docker image"
            docker_image=true
            ;;
        i) 
            ehco "installing"
            run_flag=false
            ;;
        t)
            echo "running test file"
            work_file="$test_file"
            ;;
        d)
            echo "running with debug keys"
            export DEBUG="t"
            ;;
        o)
            echo "console output"
            logging=false
            ;;
        s)
            echo "skip pre-run checks"
            checks_flag=false
            ;;
        p)
            echo "pulling changes from git"
            git pull
            git submodule update --recursive --remote
            ;;
        g)
            echo "generate methods.json"
            generate=true
            ;;
        f)
            echo "genereate methods_v2.json"
            generate_full=true
            ;;
        a)
            echo "generate documentation with AI"
            generate_ai=true
            ;;
        h)
            echo "Usage: ./install-run-core.sh [-i] [-f <file>] [-t] [-d <debug>] [-o] [-s] [-p] [-g] [-f] [-h]"
            echo "Options:"
            echo "  -p: pull changes from git before launch"
            echo "  -i: install dependencies"
            echo "  -t: run test file"
            echo "  -d: run with debug keys"
            echo "  -o: console output"
            echo "  -s: skip pre-run checks"
            echo "  -g: generate methods.json"
            echo "  -f: generate methods_v2.json. only run with -g"
            echo "  -d: build docker image for backend"
            echo "  -h: show this help message"
            exit 0
            ;;
        ?)
            echo "idk what you mean"
            exit 1
            ;;
    esac
done

if [ $docker_image = true ]; then
    cd src
    echo "building docker image"
    docker build \
        --build-arg MAIN_FILE="$work_file" \
        --build-arg BUILD_FILES="$build_files" \
        -t letovo-server:latest .
    docker save letovo-server:latest -o letovo-server-docker.tar
    exit 0
fi

if [ $run_flag = true ]; then
    if [ $generate = true ]; then
        .venv/bin/python3 ./docs/search_methods.py
        if [ $generate_full = true ]; then
            .venv/bin/python3 ./docs/search_responces.py
        fi
        if [ $generate_ai = true ]; then
            .venv/bin/python3 ./docs/ai_gen_docs.py
        fi
        exit 0
    fi
    export MAIN_FILE="$work_file"
    export BUILD_FILES="$build_files"
    echo "running $work_file"
    echo "backend server should be avaluable on localhost/api"
    cd src
    rm ./server_starter
    if [ $logging = true ]; then
        cmake . &>> ./launch_logs/build_"$(date '+%d-%m-%Y_%H:%M:%S')".log 2>&1
        cmake --build . 2>> ./launch_logs/build_"$(date '+%d-%m-%Y_%H:%M:%S')".log
        ./server_starter &> ./launch_logs/run_"$(date '+%d-%m-%Y_%H:%M:%S')".log
    else
        cmake . 
        cmake --build .
        ./server_starter
    fi
else
    python3 -m venv .venv
    .venv/bin/pip3 install -r ./requirements.txt
    # detect system packet manager
    declare -A osInfo;
    osInfo[/etc/arch-release]="pacman -S"
    osInfo[/etc/debian_version]="apt-get install"

    # install pacages
    for f in ${!osInfo[@]}
    do
        if [[ -f $f ]];then
            pack_manager=${osInfo[$f]}
        fi
    done
    if [ -z ${pack_manager+x} ]; then 
        echo "I have no idea, what this system is, sorry"
        exit 255
    fi

    echo "install c++ compiler"
    if [ "$pack_manager" == "pacman -S" ]; then
      sudo pacman -Syu base-devel
      sudo pacman -S restinio
    else
       sudo apt-get install build-essential
       sudo apt-get install librestinio-dev
       sudo apt-get install libqrencode-dev
       sudo apt-get install libpng-dev
    fi
    echo "install nginx"
    sudo $pack_manager nginx
    sudo "prepare nginx"
    sudo cp ./docs/nginx.conf /etc/nginx/nginx.conf
    sudo rm -rf /etc/nginx/certs
    sudo cp -r ./certs /etc/nginx
    sudo ln -s /etc/nginx/certs/letovocorp.ru /etc/nginx/certs/default #TODO: Customizatio
    sudo systemctl restart nginx
    echo "install cmake"
    sudo $pack_manager cmake 
    echo "install jq"
    sudo $pack_manager jq
    echo "install pkg-config"
    sudo $pack_manager pkg-config
    echo "install nlohmann-json3-dev"
    sudo $pack_manager nlohmann-json3-dev
    echo "install libasio-dev"
    sudo $pack_manager libasio-dev
    echo "install rapidjson-dev"
    sudo $pack_manager rapidjson-dev
    echo "install libpqxx-dev"
    sudo $pack_manager libpqxx-dev
    
fi
