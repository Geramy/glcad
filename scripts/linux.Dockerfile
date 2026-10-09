# The Linux packaging environment: everything the build needs and nothing the
# host happens to have. Built once, cached; `scripts/package-linux.sh` runs
# the actual build inside it with the repo bind-mounted.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang cmake ninja-build git ca-certificates \
        glslang-tools libx11-dev \
        flatbuffers-compiler libflatbuffers-dev libssl-dev fonts-dejavu-core \
        rpm file dpkg-dev \
        bison flex zlib1g-dev pkg-config libpoppler-cpp-dev \
        libpng-dev libjpeg-dev libtiff-dev libgxps-dev cups-client \
    && rm -rf /var/lib/apt/lists/*
