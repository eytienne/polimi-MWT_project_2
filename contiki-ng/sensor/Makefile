all: room-sensor

PROJECT_SOURCEFILES += module.cpp
LD_OVERRIDE = g++

MODULES += os/net/app-layer/mqtt

CONTIKI = ../..
#CFLAGS	+= -Wno-nonnull-compare -Wno-implicit-function-declaration

fetch-include:
	cd include && \
	curl --output json.hpp https://raw.githubusercontent.com/nlohmann/json/v3.11.2/single_include/nlohmann/json.hpp

include $(CONTIKI)/Makefile.include
