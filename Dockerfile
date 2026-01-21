# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
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

# Add labels for container metadata
LABEL org.opencontainers.image.title="Popeye" \
      org.opencontainers.image.description="Chess problem solving program with spinach parallelization wrapper" \
      org.opencontainers.image.source="https://github.com/philsphicas/popeye"

# Install runtime dependencies for spinach (Tcl + tcllib)
RUN apt-get update && apt-get install -y --no-install-recommends \
    tcl \
    tcllib \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash popeye

# Copy popeye binary and spinach script
COPY --from=builder /build/py /usr/local/bin/py
COPY --from=builder /build/spinach.tcl /usr/local/bin/spinach.tcl

# Create wrapper scripts
RUN chmod +x /usr/local/bin/spinach.tcl && \
    echo '#!/bin/sh' > /usr/local/bin/spinach && \
    echo 'exec tclsh /usr/local/bin/spinach.tcl "$@"' >> /usr/local/bin/spinach && \
    chmod +x /usr/local/bin/spinach && \
    echo '#!/bin/sh' > /usr/local/bin/entrypoint.sh && \
    echo 'case "$1" in' >> /usr/local/bin/entrypoint.sh && \
    echo '  py) shift; exec py "$@" ;;' >> /usr/local/bin/entrypoint.sh && \
    echo '  spinach) shift; exec spinach "$@" ;;' >> /usr/local/bin/entrypoint.sh && \
    echo '  *) exec py "$@" ;;' >> /usr/local/bin/entrypoint.sh && \
    echo 'esac' >> /usr/local/bin/entrypoint.sh && \
    chmod +x /usr/local/bin/entrypoint.sh

USER popeye
WORKDIR /home/popeye

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
