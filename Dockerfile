FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY src/ src/
COPY vendor/ vendor/
COPY server/ server/
COPY Makefile .

RUN make magpie_server BUILD=release

FROM ubuntu:24.04 AS data

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY download_data.sh .
COPY convert_lexica.sh .
COPY src/ src/
COPY Makefile .

RUN ./download_data.sh

# Build magpie CLI for lexicon conversion
RUN apt-get update && apt-get install -y --no-install-recommends build-essential && \
    rm -rf /var/lib/apt/lists/*

COPY cmd/ cmd/
RUN make magpie BUILD=release && ./convert_lexica.sh

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY --from=builder /magpie/bin/magpie_server bin/magpie_server
COPY --from=data /magpie/data/ data/
COPY --from=data /magpie/testdata/ testdata/

EXPOSE 8080

ENTRYPOINT ["bin/magpie_server", "--data", "./data:./testdata"]
