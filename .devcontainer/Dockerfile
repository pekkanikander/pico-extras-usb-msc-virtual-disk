# Use an official Ubuntu base image (you can change the version as needed)
FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Update and install required packages
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    clang \
    lld \
    libclang-rt-dev-wasm32 \
    gcc-arm-none-eabi \
    git \
    make \
    nodejs \
    wget \
    jq \
    zstd \
  && rm -rf /var/lib/apt/lists/*

# Install Pico SDK
RUN \
    mkdir -p /pico-sdk/ && \
    cd /pico-sdk/ && \
    git clone https://github.com/raspberrypi/pico-sdk.git --branch master && \
    cd pico-sdk/ && \
    git submodule update --init && \
    cd /

# Set the Pico SDK environment variable
ENV PICO_SDK_PATH=/pico-sdk/pico-sdk/


# Set the default command (optional)
CMD ["bash"]
