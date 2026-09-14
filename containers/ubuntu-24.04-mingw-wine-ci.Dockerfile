FROM ubuntu@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       ca-certificates \
       cmake \
       curl \
       g++-mingw-w64-x86-64-posix \
       git \
       ninja-build \
       perl \
       pkg-config \
       tar \
       unzip \
       wine64 \
       zip \
    && rm -rf /var/lib/apt/lists/*

ENV PATH=/usr/lib/wine:${PATH}
ENV WINEARCH=win64
ENV WINEDEBUG=-all
ENV WINEPREFIX=/tmp/mmxisf-wine

WORKDIR /work
