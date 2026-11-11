# syntax=docker/dockerfile:1.7
#
# Build environment for the Hothouse / Daisy Seed firmware in this repo.
#
# The image carries the Arm cross-compiler plus prebuilt libDaisy and DaisySP,
# so the host needs nothing except Docker (to build) and dfu-util (to flash).
# Flashing stays on the host because Docker Desktop on macOS cannot pass USB
# devices through to containers.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# g++ is the native compiler, used only for the offline DSP tests.
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates curl bzip2 git make g++; \
    rm -rf /var/lib/apt/lists/*

# Same toolchain generation the Daisy ecosystem builds against.
ARG ARM_TOOLCHAIN_VERSION=gcc-arm-none-eabi-10-2020-q4-major
ARG TARGETARCH
RUN set -eux; \
    case "${TARGETARCH}" in \
        amd64) arch="x86_64";  md5="8312c4c91799885f222f663fc81f9a31" ;; \
        arm64) arch="aarch64"; md5="1c3b8944c026d50362eef1f01f329a8e" ;; \
        *) echo "Unsupported architecture: ${TARGETARCH}"; exit 1 ;; \
    esac; \
    archive="${ARM_TOOLCHAIN_VERSION}-${arch}-linux.tar.bz2"; \
    curl -fsSL "https://developer.arm.com/-/media/Files/downloads/gnu-rm/10-2020q4/${archive}" -o "/tmp/${archive}"; \
    echo "${md5}  /tmp/${archive}" | md5sum -c -; \
    tar -xjf "/tmp/${archive}" -C /opt; \
    rm "/tmp/${archive}"
ENV PATH="/opt/${ARM_TOOLCHAIN_VERSION}/bin:${PATH}"

# libDaisy v9.0.0 is the first release that detects the Daisy Seed 3.
# The HothouseExamples repo pins a March 2024 commit that predates it.
ARG LIBDAISY_REF=v9.0.0
RUN set -eux; \
    git clone --depth 1 --branch "${LIBDAISY_REF}" \
        --recurse-submodules --shallow-submodules \
        https://github.com/electro-smith/libDaisy.git /opt/libDaisy; \
    make -j"$(nproc)" -C /opt/libDaisy; \
    test -f /opt/libDaisy/build/libdaisy.a

ARG DAISYSP_REF=2c72eaf9eac5fc0dca1919d65d606da907832618
RUN set -eux; \
    mkdir /opt/DaisySP; cd /opt/DaisySP; \
    git init -q; \
    git remote add origin https://github.com/electro-smith/DaisySP.git; \
    git fetch -q --depth 1 origin "${DAISYSP_REF}"; \
    git checkout -q FETCH_HEAD; \
    git submodule update --init --recursive --depth 1; \
    make -j"$(nproc)"; \
    test -f build/libdaisysp.a; \
    if [ ! -f DaisySP-LGPL/build/libdaisysp-lgpl.a ]; then make -j"$(nproc)" -C DaisySP-LGPL; fi; \
    test -f DaisySP-LGPL/build/libdaisysp-lgpl.a

ENV LIBDAISY_DIR=/opt/libDaisy \
    DAISYSP_DIR=/opt/DaisySP

WORKDIR /work
