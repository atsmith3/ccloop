FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    cmake \
    ninja-build \
    libcurl4-openssl-dev \
    ca-certificates \
    git \
    gdb \
    valgrind \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Create builder user -- matches the pattern used for Yocto builds
# At docker run time, use -u $(id -u):$(id -g) to override UID/GID so that
# files written inside the container have the same ownership as the host user
RUN useradd -ms /bin/bash builder && \
    echo "builder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER builder
WORKDIR /workspace

CMD ["/bin/bash"]
