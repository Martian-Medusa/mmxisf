FROM ubuntu@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       ca-certificates \
       clang \
       cmake \
       doxygen \
       git \
       libclang-rt-18-dev \
       libexpat1-dev \
       liblz4-dev \
       libssl-dev \
       libzstd-dev \
       ninja-build \
       zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
