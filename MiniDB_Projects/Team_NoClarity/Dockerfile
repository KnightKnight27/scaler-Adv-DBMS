# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN mkdir build && cd build && cmake .. && make

# Run stage
FROM debian:bookworm-slim
WORKDIR /app

COPY --from=builder /app/build/minidb_test /app/minidb_test
COPY --from=builder /app/build/minidb_benchmark /app/minidb_benchmark

# Default to running the test suite
CMD ["/app/minidb_test"]
