FROM ubuntu:24.04

# gcc/make build the batch and the console; python3 runs the order generator;
# ttyd is only there to put the line-mode console in a browser (make docker-web).
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc libc6-dev make python3 ttyd \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/futmatch
COPY Makefile ./
COPY src/ src/
COPY run/ run/
COPY tools/ tools/
COPY ctl/ ctl/
COPY data/ data/

RUN make build \
 && chmod +x run/*.sh \
 && mkdir -p output

ENV FUTHOME=/opt/futmatch
EXPOSE 7681
CMD ["./run/OPCON.sh"]
