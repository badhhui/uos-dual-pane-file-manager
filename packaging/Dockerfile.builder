FROM debian:buster-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN printf 'deb http://archive.debian.org/debian buster main\ndeb http://archive.debian.org/debian-security buster/updates main\n' > /etc/apt/sources.list \
 && apt-get -o Acquire::Check-Valid-Until=false -o Acquire::Retries=8 update \
 && apt-get -o Acquire::Check-Valid-Until=false -o Acquire::Retries=8 install -y --no-install-recommends \
      build-essential cmake qtbase5-dev qtbase5-dev-tools fonts-noto-cjk python3-minimal xz-utils file zlib1g-dev libsqlite3-dev poppler-utils p7zip-full zip unzip \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work/file-manager-native
