FROM ubuntu:20.04

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
    libssl-dev
RUN git clone https://github.com/drogonframework/drogon && \
    cd drogon && \
    git submodule update --init && \
    mkdir build && \ 
    cd build && \
    cmake .. && \
    make && make install && cd /App
# RUN mkdir /usr/local/lib/pkgconfig && DISABLE_WARNING_AS_ERROR=1 make install -j10
# RUN DISABLE_WARNING_AS_ERROR=1 USE_RTTI=1 make kv_server
