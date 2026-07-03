# ------ Stage 1: build -------
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем инструменты и dev-пакеты для сборки
RUN apt update && apt install -y \
        build-essential \
        cmake \
        pkg-config \
        git \
        wget \
        curl \
        libfmt-dev \
        librestinio-dev \
        libasio-dev \
        rapidjson-dev \
        libpqxx-dev \
        libpq-dev \
        libssl-dev \
        libqrencode-dev \
        libpng-dev \
    && rm -rf /var/lib/apt/lists/*

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
RUN mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j"$(nproc)"
RUN cp /app/build/server_starter /app/server_starter && strip /app/server_starter || true

# ------ Stage 2: runtime -------
FROM ubuntu:24.04

# Устанавливаем только runtime-библиотеки
RUN apt update && apt install -y \
        libfmt9 \
        libhttp-parser2.9 \
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
