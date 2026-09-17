#!/bin/bash
set -e

source "$(dirname "$(realpath "$0")")/scripts/common.sh"

WORKSPACE="$(basename "${BASE_DIR}")"

docker build -t ${WORKSPACE} ${BASE_DIR}

ENV_FILE_ARG=()
if [ -f "${HOME}/.env" ]; then
    ENV_FILE_ARG+=("--env-file=${HOME}/.env")
fi

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
    "${ENV_FILE_ARG[@]}"
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
