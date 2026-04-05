FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY src/ src/
COPY docs/ docs/
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

COPY data/layouts/wwf15.txt data/layouts/wwf15.txt
COPY data/letterdistributions/wwf_english.csv data/letterdistributions/wwf_english.csv
COPY data/lexica/ENABLE.txt data/lexica/ENABLE.txt

RUN make magpie BUILD=release 2>&1 && ./convert_lexica.sh 2>&1
RUN bin/magpie convert text2kwg ENABLE && \
    bin/magpie convert text2wordmap ENABLE -threads 4 && \
    bin/magpie convert klv2csv NWL23 && \
    grep -v '^NNNNNN,' data/lexica/NWL23.csv > data/lexica/ENABLE.csv && \
    bin/magpie convert csv2klv ENABLE

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /magpie

COPY --from=builder /magpie/bin/magpie_server bin/magpie_server
COPY --from=builder /magpie/docs/openapi.yaml docs/openapi.yaml
COPY --from=data /magpie/data/ data/
COPY --from=data /magpie/testdata/ testdata/

EXPOSE 8080

ENTRYPOINT ["bin/magpie_server", "--data", "./data:./testdata"]
