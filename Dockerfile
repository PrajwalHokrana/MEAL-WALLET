# ============================================================================
# Meal Wallet — Multi-Stage Cloud Deployment Dockerfile (Zero SQL Dependencies)
# Reads PORT, DATA_DIR, ADMIN_SEED_PASSWORD, SESSION_SECRET from environment.
# ============================================================================
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY CMakeLists.txt Makefile ./
COPY include ./include
COPY third_party ./third_party
COPY src ./src

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2

FROM debian:bookworm-slim AS runtime
WORKDIR /app

COPY --from=builder /app/build/mealwallet /app/mealwallet
COPY web /app/web

ENV PORT=8080
ENV DATA_DIR=/app/data
ENV ADMIN_SEED_PASSWORD=admin123
ENV SESSION_SECRET=mealwallet-cloud-session-secret

RUN mkdir -p /app/data
EXPOSE 8080

CMD ["/app/mealwallet"]
