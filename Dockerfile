# linux/amd64 Debian Bookworm manifest resolved from registry-1.docker.io on 2026-08-14.
ARG DEBIAN_BASE=debian:bookworm-slim@sha256:362e64223cc0da95422b3b13c045186fc0a81250e765d31c025fbddf257f6143

FROM ${DEBIAN_BASE} AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        ca-certificates \
        cmake \
        git \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

FROM ${DEBIAN_BASE} AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        curl \
        libgcc-s1 \
        libstdc++6 \
    && groupadd --gid 10001 devmanager \
    && useradd --uid 10001 --gid 10001 --create-home --home-dir /nonexistent --shell /usr/sbin/nologin devmanager \
    && install --directory --owner=10001 --group=10001 /var/lib/devmanager /var/log/devmanager \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/devmanager
COPY --from=builder /workspace/build/devmanager_http /opt/devmanager/devmanager_http
COPY deploy/docker/config/devmanager.json /opt/devmanager/config/devmanager.json

USER 10001:10001
ENTRYPOINT ["/opt/devmanager/devmanager_http"]
