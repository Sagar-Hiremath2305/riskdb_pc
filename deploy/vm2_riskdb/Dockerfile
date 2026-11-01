FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    libomp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY apps/ ./apps/
COPY tests/ ./tests/

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target riskdb_app riskdb -j$(nproc)

FROM ubuntu:24.04 AS runner
RUN apt-get update && apt-get install -y libpq5 libomp5 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /app/build/riskdb_app /app/build/riskdb ./

ENTRYPOINT ["./riskdb_app"]
