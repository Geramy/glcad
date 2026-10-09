# Runtime smoke tools layered onto the matching Linux package build image.
ARG BASE_IMAGE=glcad-linux-build-amd64
FROM ${BASE_IMAGE}
ARG TEST_WINE=0
RUN apt-get update && apt-get install -y --no-install-recommends \
      libvulkan1 mesa-vulkan-drivers xvfb xauth xdotool \
    && if [ "$TEST_WINE" = 1 ]; then dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends wine64 wine32:i386; fi \
    && rm -rf /var/lib/apt/lists/*
