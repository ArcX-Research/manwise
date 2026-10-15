FROM ubuntu:24.04
RUN sed -i '\|^path-exclude=/usr/share/man/|d' /etc/dpkg/dpkg.cfg.d/excludes
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc clang libclang-rt-18-dev make libc6-dev python3 man-db manpages manpages-dev groff-base procps \
    && rm -rf /var/lib/apt/lists/*
RUN if [ -f /usr/bin/man.REAL ]; then \
    rm /usr/bin/man && dpkg-divert --quiet --remove --rename /usr/bin/man; fi
RUN useradd --create-home tester
WORKDIR /work
COPY --chown=tester:tester . /work
RUN chown tester:tester /work
USER tester
CMD ["sh", "-c", "make test CC=gcc BUILD=build/gcc && make test CC=clang BUILD=build/clang && make sanitize"]
