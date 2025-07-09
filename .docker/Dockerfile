FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libssl-dev \
    zlib1g-dev \
    python3 \
    python3-pip \
    libxml2-dev \
    git

# Install PyYAML
RUN pip install PyYAML
