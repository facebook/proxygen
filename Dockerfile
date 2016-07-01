FROM ubuntu

RUN apt-get update && apt-get install -yq \
    autoconf-archive \
    bison \
    build-essential \
    cmake \
    curl \
    flex \
    git \
    gperf \
    libcap-dev \
    libevent-dev \
    libgoogle-glog-dev \
    libkrb5-dev \
    libnuma-dev \
    libsasl2-dev \
    libssl-dev \
    pkg-config \
    sudo \
    unzip \
    wget

WORKDIR /home
RUN git clone https://github.com/facebook/proxygen.git
WORKDIR /home/proxygen/proxygen
RUN ./deps.sh && ./reinstall.sh
WORKDIR /home/proxygen/proxygen/httpserver/samples/echo
RUN g++ -I /home/proxygen -std=c++11 -o my_echo EchoServer.cpp EchoHandler.cpp -lproxygenhttpserver -lfolly -lglog -lgflags -pthread
