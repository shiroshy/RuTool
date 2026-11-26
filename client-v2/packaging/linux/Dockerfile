# Ubuntu base is pinned; apt and Python dependency versions can change.
ARG BASE_IMAGE=ubuntu:22.04@sha256:b000e69a50ff940390233025e7a337fb8469b509e2334fa4d018ac30f3e18cdb
FROM ${BASE_IMAGE} AS sdk-bootstrap
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential g++-12 ninja-build python3 python3-venv \
    ca-certificates curl file patchelf binutils squashfs-tools desktop-file-utils \
    libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xfixes0 libxcb-xinerama0 \
    libdbus-1-3 libfontconfig1 libfreetype6 libwayland-client0 libwayland-cursor0 \
    libwayland-egl1 libssl3 libx11-xcb1 libxrender1 libsm6 libice6 \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 cmake==3.31.6
ENV PATH="/opt/aqt/bin:/opt/Qt/6.8.3/gcc_64/bin:${PATH}" \
    CMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64 \
    QMAKE=/opt/Qt/6.8.3/gcc_64/bin/qmake \
    CC=gcc-12 CXX=g++-12 \
    RUTOOL_TOOLS_DIR=/tools \
    APPIMAGE_EXTRACT_AND_RUN=1
WORKDIR /work
# Mount a SHA256-locked deployment tool directory read-only at /tools.
# Sources are mounted separately; the image contains only the build SDK.
CMD ["bash"]

# On space-constrained hosts, build --target sdk-bootstrap and mount an SDK at /opt/Qt.
FROM sdk-bootstrap AS builder
RUN /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
        --outputdir /opt/Qt --modules qtimageformats qtshadertools
