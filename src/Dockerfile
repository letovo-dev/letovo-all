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
RUN rm -rf CMakeFiles

# Передаём CMake переменные MAIN_FILE и BUILD_FILES из build-args
ARG MAIN_FILE
ARG BUILD_FILES
ENV MAIN_FILE=${MAIN_FILE}
ENV BUILD_FILES=${BUILD_FILES}

# Собираем проект
RUN mkdir build && cd build && cmake .. && make

# ------ Stage 2: runtime -------
FROM ubuntu:24.04

# Устанавливаем только runtime-библиотеки
RUN apt update && apt install -y \
        libfmt9 \
        libhttp-parser-dev \
        libpqxx-dev \
        libpq-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Копируем готовый бинарник из стадии сборки
COPY --from=builder /app/build/server_starter .

# Копируем все файлы с "Config" в имени рядом с бинарем
COPY --from=builder /app/*Config* ./

COPY --from=builder /app/pages /app/pages

EXPOSE 8080

CMD ["./server_starter"]
