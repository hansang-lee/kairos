FROM debian:12-slim AS builder

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update \
 && apt-get install -y --fix-missing --no-install-recommends \
    sudo curl ca-certificates cmake make libstdc++6 gcc g++ clang-format ninja-build \
    libcurl4-openssl-dev nlohmann-json3-dev \
 && update-ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && apt-get clean

ARG USERNAME="admin"
ARG UID="1000"
ARG GID="1000"
RUN groupadd --gid ${GID} ${USERNAME} \
 && useradd --uid ${UID} --gid ${GID} -m ${USERNAME} \
 && usermod -a -G video ${USERNAME} \
 && usermod -a -G dialout ${USERNAME} \
 && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
 && chmod 0440 /etc/sudoers.d/${USERNAME}
