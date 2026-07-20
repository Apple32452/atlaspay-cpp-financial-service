FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake libsqlite3-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel && \
    ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libsqlite3-0 ca-certificates && \
    useradd --system --create-home --home-dir /var/lib/atlaspay atlaspay && \
    rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/atlaspay_server /usr/local/bin/atlaspay_server
USER atlaspay
WORKDIR /var/lib/atlaspay
ENV ATLASPAY_DB_PATH=/var/lib/atlaspay/atlaspay.db \
    ATLASPAY_BIND=0.0.0.0 \
    ATLASPAY_PORT=8080
EXPOSE 8080
VOLUME ["/var/lib/atlaspay"]
ENTRYPOINT ["/usr/local/bin/atlaspay_server"]
