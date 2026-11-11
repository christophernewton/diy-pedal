# Host-side entry points. Everything except flashing runs inside Docker.
#
#   make build   compile the firmware        -> firmware/build/bass_fx.bin
#   make test    run the offline DSP checks
#   make demo    render each effect to a WAV -> build/demo/
#   make flash   build, then flash over USB (pedal must be in DFU mode)
#   make shell   open a shell in the build container
#   make clean   remove build output

IMAGE := diy-pedal-build
BIN   := firmware/build/bass_fx.bin
STAMP := build/.image-stamp

DOCKER_RUN := docker run --rm -v "$(CURDIR)":/work -w /work $(IMAGE)
TEST_BUILD := g++ -std=gnu++14 -O2 -Wall -Wextra -Ifirmware \
	-I/opt/DaisySP/DaisySP-LGPL/Source tests/test_dsp.cpp \
	/opt/DaisySP/DaisySP-LGPL/Source/Effects/reverbsc.cpp -o build/test_dsp

.PHONY: build test demo flash shell clean image

build: $(STAMP)
	$(DOCKER_RUN) make -C firmware -j4
	@echo
	@echo "Firmware ready: $(BIN)"

test: $(STAMP)
	$(DOCKER_RUN) sh -c '$(TEST_BUILD) && ./build/test_dsp'

demo: $(STAMP)
	@mkdir -p build/demo
	$(DOCKER_RUN) sh -c '$(TEST_BUILD) && ./build/test_dsp --render build/demo'

flash: build
	./scripts/flash.sh $(BIN)

shell: $(STAMP)
	docker run --rm -it -v "$(CURDIR)":/work -w /work $(IMAGE) bash

clean:
	rm -rf build firmware/build

image: $(STAMP)

# Rebuild the image only when the Dockerfile changes. The first build takes
# a few minutes because it compiles libDaisy and DaisySP.
$(STAMP): Dockerfile
	docker build -t $(IMAGE) .
	@mkdir -p build
	@touch $(STAMP)
