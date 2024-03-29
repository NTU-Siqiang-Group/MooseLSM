FROM ubuntu:22.04

ADD . /App
WORKDIR /App

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y libgflags-dev \
    libsnappy-dev \
    zlib1g-dev \
    libbz2-dev \
    liblz4-dev \
    libzstd-dev \
    make \
    cmake \
    g++ \
    gcc \
    git \
    libjsoncpp-dev \
    uuid-dev \
    openssl \
    curl \
    unzip \
    ca-certificates \
    libssl-dev
RUN git clone https://github.com/drogonframework/drogon.git && \
    cd drogon && \
    git submodule update --init && \
    mkdir build && \ 
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j4 && make install && cd /App
RUN rm -rf build && mkdir build && cd build && cmake -DFAIL_ON_WARNINGS=0 -DUSE_RTTI=1 -DCMAKE_BUILD_TYPE=Release .. && make kv_server -j20
