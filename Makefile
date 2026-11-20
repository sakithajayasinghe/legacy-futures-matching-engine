IMAGE  ?= futmatch-legacy
CC     ?= gcc
CFLAGS ?= -std=gnu89 -O2 -Wall -Wextra

.PHONY: build run console gen docker-build docker-run docker-web docker-batch clean

build: bin/futmatch bin/opcon

bin/futmatch: src/futmatch.c src/futmatch.h src/futtab.h
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/futmatch src/futmatch.c

bin/opcon: src/opcon.c
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/opcon src/opcon.c

# batch on the sample file, output in ./output/<timestamp>/
run: build
	./run/RUNSESSION.sh

# operator console against ./output
console: build
	FUTHOME="$(CURDIR)" ./bin/opcon

gen:
	python3 tools/gen_orders.py --count 10000 --seed 14 --out data/ORDERS.DAT

docker-build:
	docker build -t $(IMAGE) .

# operator console in the container; sessions are kept in ./output
docker-run:
	mkdir -p output
	docker run --rm -i $(shell [ -t 0 ] && echo -t) -v "$(CURDIR)/output:/opt/futmatch/output" $(IMAGE)

# the same console in a browser: http://localhost:7681
docker-web:
	mkdir -p output
	docker run --rm -p 7681:7681 -v "$(CURDIR)/output:/opt/futmatch/output" $(IMAGE) \
		ttyd -W -p 7681 ./run/OPCON.sh

# batch only, on the sample file
docker-batch:
	mkdir -p output
	docker run --rm -v "$(CURDIR)/output:/opt/futmatch/output" $(IMAGE) ./run/NEWDAY.sh data/ORDERS.DAT

clean:
	rm -rf bin output
