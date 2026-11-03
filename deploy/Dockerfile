# Ask the Gaffer: multi-stage image with every binary and a pre-partitioned index.
#
#   docker compose -f deploy/docker-compose.yml up --build
#
# Stage 1 compiles Release binaries (tests off, so GoogleTest isn't fetched)
# and runs `gaffer partition` to bake the shards into the image. Stage 2 is a
# slim runtime: binaries + shard files + nickname list, run as a non-root user.

FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake ninja-build \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake cmake
COPY src src
COPY apps apps
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAFFER_BUILD_TESTS=OFF \
 && cmake --build build
COPY data/corpus data/corpus
ARG SHARDS=4
RUN ./build/apps/gaffer partition --shards ${SHARDS} --out-dir /shards \
      --corpus data/corpus/pl-history.tsv --corpus data/corpus/pl-2025-26.tsv

FROM ubuntu:24.04
RUN useradd --system --no-create-home gaffer
COPY --from=build /src/build/apps/gaffer /src/build/apps/gaffer-shard \
                  /src/build/apps/gaffer-broker /usr/local/bin/
COPY --from=build /shards /shards
COPY data/nicknames.tsv /data/nicknames.tsv
# The tools look for data/nicknames.tsv relative to the working directory.
WORKDIR /
USER gaffer
EXPOSE 7000 7001
