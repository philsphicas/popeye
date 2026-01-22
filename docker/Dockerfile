# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Remove any cached build artifacts and build with gcc toolchain
RUN find . -name '*.o' -delete && \
    find . -name '*.a' -delete && \
    find . -name 'depend' -delete && \
    rm -f py gengmarr && \
    make -f makefile.unx TOOLCHAIN=gcc -j$(nproc) && \
    strip py

# Runtime stage
FROM debian:bookworm-slim

# Install runtime dependencies for spinach (Tcl + tcllib) and debugging tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    tcl \
    tcllib \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash popeye

# Copy popeye binary, spinach script, and wrapper scripts
COPY --from=builder /build/py /usr/local/bin/py
COPY --from=builder /build/spinach.tcl /usr/local/bin/spinach.tcl
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
COPY docker/spinach /usr/local/bin/spinach

RUN chmod +x /usr/local/bin/spinach.tcl \
             /usr/local/bin/spinach \
             /usr/local/bin/entrypoint.sh

USER popeye
WORKDIR /home/popeye

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
