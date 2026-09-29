# Auto-generated from hashlink's Makefile template; do not edit

UNAME := $(shell uname)
CFLAGS = -std=c2x $(EXTRA_CFLAGS)
CPPFLAGS = ::if (jumboBuild != "true")::-DHL_MAKE ::end::-I$$makePath(::relDir::)
LDFLAGS = $(EXTRA_LDFLAGS)

ifdef DEBUG
CFLAGS += -g
else
CFLAGS += -O3
endif

ifdef HASHLINK
CPPFLAGS += -I$(HASHLINK)/include -I$(HASHLINK)/src
LDFLAGS += -L$(HASHLINK)/lib -L$(HASHLINK)
else
HASHLINK = /usr/local
endif

LIMEN_DIR := $(firstword $(foreach dir,$(HASHLINK)/lib $(HASHLINK)/bin $(HASHLINK),$(if $(wildcard $(dir)/*.limen),$(dir))))
LIMEN_FILES := $(if $(LIMEN_DIR),$(wildcard $(LIMEN_DIR)/*.limen))

ifeq ($(UNAME),Darwin)
LDLIBS = -lhl -lm::foreach libraries::::if (name == "uv"):: -luv::end:: $(HASHLINK)/lib/::name::.hdll::end::
LDFLAGS += -rpath $(HASHLINK)/lib
else
LDLIBS = -lhl -lm::foreach libraries::::if (name == "uv"):: -luv::end:: -l:::name::.hdll::end::
LDFLAGS += -Wl,-rpath $(HASHLINK)/lib
endif

ifeq ($(OS),Windows_NT)
LDFLAGS+=-municode
LDLIBS+=-ldbghelp
EXE_SUFFIX=.exe
endif
::if (jumboBuild == "true")::
::name::$(EXE_SUFFIX): ::name::.c | copy-limen
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)
::else::
SRC_FILES=::foreach cfiles:: \
	::path::::end::

OBJ_FILES=$(patsubst %.c,%.o,$(SRC_FILES))

DEP_FILES=$(patsubst %.c,%.d,$(SRC_FILES))

RSP_FILE = objects.txt
$(shell rm -f $(RSP_FILE))
$(foreach o,$(OBJ_FILES),$(eval $(shell printf '%s\n' '$(o)' >> $(RSP_FILE))))

::name::$(EXE_SUFFIX): $(OBJ_FILES) | copy-limen
	$(CC) @$(RSP_FILE) -o $@ $(LDFLAGS) $(LDLIBS)

-include $(DEP_FILES)

%.o:
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c ::relDir::$*.c -o $@
::end::
.PHONY: copy-limen
copy-limen:
	$(if $(LIMEN_FILES),cp $(LIMEN_FILES) .)

clean:
	find . \( -name '*.o' -o -name '*.d' \) -delete
	rm -f ::name::$(EXE_SUFFIX) objects.txt

.SUFFIXES:
