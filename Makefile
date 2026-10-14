.DEFAULT_GOAL := all

APPNAME := nullnoise
LABEL := NullNoise
PACKAGENAME := com.snappercayt.nullnoise
RAWDRAWANDROID := third_party/rawdrawandroid
BUILD_DIR := build
RNNOISE := third_party/rnnoise
RNNOISE_MODEL_HASH := $(shell cat $(RNNOISE)/model_version)
RNNOISE_MODEL_ARCHIVE := $(BUILD_DIR)/rnnoise_data-$(RNNOISE_MODEL_HASH).tar.gz
RNNOISE_MODEL_DIR := $(BUILD_DIR)/rnnoise
RNNOISE_MODEL_C := $(RNNOISE_MODEL_DIR)/src/rnnoise_data.c
RNNOISE_SOURCES := \
	$(RNNOISE)/src/denoise.c \
	$(RNNOISE)/src/rnn.c \
	$(RNNOISE)/src/pitch.c \
	$(RNNOISE)/src/kiss_fft.c \
	$(RNNOISE)/src/celt_lpc.c \
	$(RNNOISE)/src/nnet.c \
	$(RNNOISE)/src/nnet_default.c \
	$(RNNOISE)/src/parse_lpcnet_weights.c \
	$(RNNOISE)/src/rnnoise_tables.c
SRC := src/main.c src/audio.c src/wav.c $(RNNOISE_SOURCES) $(RNNOISE_MODEL_C)
CFLAGS += -pthread \
	-I$(RNNOISE)/include \
	-I$(RNNOISE)/src \
	-I$(RNNOISE_MODEL_DIR)/src
LDFLAGS += -pthread

ANDROIDVERSION ?= 30
ANDROIDTARGET ?= 35
TARGETS := $(BUILD_DIR)/makecapk/lib/arm64-v8a/lib$(APPNAME).so
override KEYSTOREFILE := $(BUILD_DIR)/nullnoise-debug.keystore
override DNAME := "CN=NullNoise Debug, O=NullNoise"
override ALIASNAME := nullnoise-debug

$(BUILD_DIR):
	mkdir -p $@

$(RNNOISE_MODEL_ARCHIVE): | $(BUILD_DIR)
	curl --fail --location --silent --show-error \
		"https://media.xiph.org/rnnoise/models/rnnoise_data-$(RNNOISE_MODEL_HASH).tar.gz" \
		-o $@.tmp
	echo "$(RNNOISE_MODEL_HASH)  $@.tmp" | sha256sum --check
	mv $@.tmp $@

$(RNNOISE_MODEL_C): $(RNNOISE_MODEL_ARCHIVE)
	mkdir -p $(RNNOISE_MODEL_DIR)
	tar -xzf $< -C $(RNNOISE_MODEL_DIR) src/rnnoise_data.c src/rnnoise_data.h

$(KEYSTOREFILE): | $(BUILD_DIR)

include $(RAWDRAWANDROID)/Makefile
