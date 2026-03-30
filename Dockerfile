# Base image with CUDA 12.0 Toolkit
FROM nvidia/cuda:12.0.1-devel-ubuntu22.04

# Avoid tzdata interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install core build tools, math libraries, and PAPI
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gdb \
    clang-format \
    clang-tidy \
    libopenblas-dev \
    liblapack-dev \
    libfftw3-dev \
    libpapi-dev \
    software-properties-common \
    && rm -rf /var/lib/apt/lists/*

# Install GCC-13 for standard C++23 support
RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update \
    && apt-get install -y gcc-13 g++-13 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /sim