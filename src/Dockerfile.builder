# syntax=docker/dockerfile:1
# Issue #214 pre-start fallback pilot; this PR is not for merge.
ARG BASE_IMAGE=ubuntu@sha256:a61567bd31828687156d735ea8eb01ba4e37636e225dd6a48ba94136a70d9d61
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive
COPY backend-builder.env /opt/letovo/backend-builder.env

# The minimal base has no CA bundle yet; APT still verifies the Ubuntu-signed
# snapshot metadata and package hashes before installing the pinned CA package.
RUN set -eux; \
    . /opt/letovo/backend-builder.env; \
    printf 'Types: deb\nURIs: http://snapshot.ubuntu.com/ubuntu/%s\nSuites: noble noble-updates noble-security\nComponents: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\nCheck-Valid-Until: no\n' "$APT_SNAPSHOT" > /etc/apt/sources.list.d/ubuntu.sources; \
    apt-get -o Acquire::https::Verify-Peer=false update; \
    apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends ${APT_PACKAGES}; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    . /opt/letovo/backend-builder.env; \
    fetch() { \
      name="$1"; commit="$2"; expected="$3"; repository="$4"; \
      curl -fsSL --retry 3 "https://codeload.github.com/${repository}/tar.gz/${commit}" -o "/tmp/${name}.tar.gz"; \
      echo "${expected}  /tmp/${name}.tar.gz" | sha256sum -c -; \
      mkdir -p "/tmp/${name}"; \
      tar -xzf "/tmp/${name}.tar.gz" --strip-components=1 -C "/tmp/${name}"; \
    }; \
    fetch nlohmann-json "${NLOHMANN_JSON_COMMIT}" "${NLOHMANN_JSON_SHA256}" nlohmann/json; \
    cmake -S /tmp/nlohmann-json -B /tmp/nlohmann-json-build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/letovo -DJSON_BuildTests=OFF; \
    cmake --build /tmp/nlohmann-json-build --parallel "$(nproc)"; \
    cmake --install /tmp/nlohmann-json-build; \
    fetch jwt-cpp "${JWT_CPP_COMMIT}" "${JWT_CPP_SHA256}" Thalhammer/jwt-cpp; \
    cmake -S /tmp/jwt-cpp -B /tmp/jwt-cpp-build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/letovo \
      -DJWT_BUILD_EXAMPLES=OFF -DJWT_BUILD_TESTS=OFF; \
    cmake --build /tmp/jwt-cpp-build --parallel "$(nproc)"; \
    cmake --install /tmp/jwt-cpp-build; \
    fetch llhttp "${LLHTTP_COMMIT}" "${LLHTTP_SHA256}" nodejs/llhttp; \
    test "$(sed -n 's/^project(llhttp VERSION \([^)]*\)).*/\1/p' /tmp/llhttp/CMakeLists.txt)" = "${LLHTTP_VERSION}"; \
    cmake -S /tmp/llhttp -B /tmp/llhttp-build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/letovo \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON; \
    cmake --build /tmp/llhttp-build --parallel "$(nproc)"; \
    cmake --install /tmp/llhttp-build; \
    fetch opentelemetry-proto "${OPENTELEMETRY_PROTO_COMMIT}" "${OPENTELEMETRY_PROTO_SHA256}" open-telemetry/opentelemetry-proto; \
    fetch opentelemetry-cpp "${OPENTELEMETRY_CPP_COMMIT}" "${OPENTELEMETRY_CPP_SHA256}" open-telemetry/opentelemetry-cpp; \
    cmake -S /tmp/opentelemetry-cpp -B /tmp/opentelemetry-cpp-build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/letovo \
      -DCMAKE_PREFIX_PATH=/opt/letovo -DBUILD_SHARED_LIBS=OFF \
      -DWITH_OTLP_HTTP=ON -DWITH_OTLP_GRPC=OFF -DWITH_EXAMPLES=OFF \
      -DWITH_FUNC_TESTS=OFF -DWITH_BENCHMARK=OFF \
      -DBUILD_TESTING=OFF \
      -DFETCHCONTENT_SOURCE_DIR_OPENTELEMETRY-PROTO=/tmp/opentelemetry-proto; \
    cmake --build /tmp/opentelemetry-cpp-build --parallel "$(nproc)"; \
    cmake --install /tmp/opentelemetry-cpp-build; \
    rm -rf /tmp/*.tar.gz /tmp/*-build /tmp/nlohmann-json /tmp/jwt-cpp /tmp/llhttp /tmp/opentelemetry-proto /tmp/opentelemetry-cpp

ENV CMAKE_PREFIX_PATH=/opt/letovo
