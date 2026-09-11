# ------ Stage 1: build -------
ARG BUILDER_IMAGE=ghcr.io/letovo-dev/letovo-backend-builder@sha256:2e504e6ae88819621fa055128f0e83a15ea47be27fd66febbb53ed4e784cbd2c
FROM ${BUILDER_IMAGE} AS builder

WORKDIR /app
COPY . .

# Удаляем старые артефакты CMake, если они попали из локальной машины
RUN rm -f CMakeCache.txt
RUN rm -rf CMakeFiles build

# Передаём CMake переменные MAIN_FILE и BUILD_FILES из build-args
ARG MAIN_FILE
ARG BUILD_FILES
ENV MAIN_FILE=${MAIN_FILE}
ENV BUILD_FILES=${BUILD_FILES}

# Собираем проект
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/letovo \
    && cmake --build build --parallel "$(nproc)"
RUN cp /app/build/server_starter /app/server_starter && strip /app/server_starter || true

# ------ Stage 2: runtime -------
FROM ubuntu:24.04

# Устанавливаем только runtime-библиотеки
RUN apt update && apt install -y \
        libfmt9 \
        libcurl4 \
        libhttp-parser2.9 \
        libprotobuf32t64 \
        libpqxx-7.8 \
        libpq5 \
        libssl3 \
        libqrencode4 \
        libpng16-16 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ARG LETOVO_BUILD_SHA=unknown
ENV LETOVO_BUILD_SHA=${LETOVO_BUILD_SHA}

COPY --from=builder /app/server_starter .

EXPOSE 8080

CMD ["./server_starter", "-o"]
