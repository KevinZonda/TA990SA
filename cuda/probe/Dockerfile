FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev make binutils file \
    && rm -rf /var/lib/apt/lists/*
ENTRYPOINT ["/bin/sh", "/scripts/build.sh"]
