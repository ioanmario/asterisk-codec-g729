# Build container for codec_g729.so.
#
# IMPORTANT: an Asterisk module must be built against headers matching the
# Asterisk version running on your server. This image installs the distro's
# asterisk-dev as a convenience for CI / smoke builds. For a production .so
# that exactly matches your box, prefer ./install.sh on the box itself, or
# set ASTERISK_REF below to build the headers from source.
#
#   docker build -t codec-g729 .
#   docker run --rm -v "$PWD/out:/out" codec-g729 cp /build/codec_g729.so /out/

FROM debian:trixie-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        asterisk-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . /build

RUN git submodule update --init --recursive || true
RUN make

# The artifact is /build/codec_g729.so
CMD ["sh", "-c", "ls -l /build/codec_g729.so"]
