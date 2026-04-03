FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY src/ src/
COPY vendor/ vendor/
COPY server/ server/
COPY Makefile .

RUN mkdir -p test && make magpie_server BUILD=release

FROM ubuntu:24.04 AS data

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY download_data.sh .
COPY convert_lexica.sh .
COPY src/ src/
COPY cmd/ cmd/
COPY Makefile .

RUN mkdir -p test && ./download_data.sh
RUN make magpie BUILD=release 2>&1 && ./convert_lexica.sh 2>&1

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
