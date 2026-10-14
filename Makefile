.DEFAULT_GOAL := all

APPNAME := nullnoise
LABEL := NullNoise
PACKAGENAME := com.snappercayt.nullnoise
RAWDRAWANDROID := third_party/rawdrawandroid
SRC := src/main.c
BUILD_DIR := build

ANDROIDVERSION ?= 30
ANDROIDTARGET ?= 35
TARGETS := $(BUILD_DIR)/makecapk/lib/arm64-v8a/lib$(APPNAME).so
override KEYSTOREFILE := $(BUILD_DIR)/nullnoise-debug.keystore
override DNAME := "CN=NullNoise Debug, O=NullNoise"
override ALIASNAME := nullnoise-debug

$(BUILD_DIR):
	mkdir -p $@

$(KEYSTOREFILE): | $(BUILD_DIR)

include $(RAWDRAWANDROID)/Makefile
