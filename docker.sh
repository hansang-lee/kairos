#!/bin/bash
set -e

if command -v git &>/dev/null && git rev-parse --show-toplevel &>/dev/null; then
    BASE_DIR="$(git rev-parse --show-toplevel)"
elif BASE_DIR="$(find "$(realpath "${PWD}")" \
    -maxdepth 10 \
    -type f \
    -name .root \
    -exec dirname {} \; | head -n1)" && [ -n "${BASE_DIR}" ]; then :
else
    BASE_DIR="$(dirname "$(realpath "$0")")"
fi

WORKSPACE="$(basename "${BASE_DIR}")"

docker build -t ${WORKSPACE} ${BASE_DIR}

DOCKER_ARGS=(
    "--rm"
    "--interactive"
    "--tty"
    "--name=${WORKSPACE}"
    "--user=admin"
    "--network=host"
    "--ipc=host"
    "--privileged=true"
    "--security-opt=seccomp=unconfined"
    "--cap-add=SYS_PTRACE"
    "--env=DISPLAY=${DISPLAY}"
    "--env=NVIDIA_VISIBLE_DEVICES=all"
    "--env=NVIDIA_DRIVER_CAPABILITIES=all"
    "--env=TZ=Asia/Seoul"
    "--env-file=${HOME}/.env"
    "--mount=source=/etc/localtime,target=/etc/localtime,type=bind,consistency=cached:ro"
    "--mount=source=/etc/timezone,target=/etc/timezone,type=bind,consistency=cached:ro"
    "--mount=source=/dev,target=/dev,type=bind,consistency=cached:ro"
    "--mount=source=/tmp/.X11-unix,target=/tmp/.X11-unix,type=bind,consistency=cached"
    "--mount=source=${BASE_DIR},target=/workspaces/${WORKSPACE},type=bind"
    "--workdir=/workspaces/${WORKSPACE}"
    "${WORKSPACE}"
)

CONTAINER_COMMANDS="
    ulimit -c 0;
    /bin/bash
"

docker run "${DOCKER_ARGS[@]}" /bin/bash -c "${CONTAINER_COMMANDS}"
