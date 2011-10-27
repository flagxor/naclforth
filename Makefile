NACL_SDK_ROOT?=~/clients/naclports/src
NACL_SDK_BIN_DIR=${NACL_SDK_ROOT}/toolchain/mac_x86_newlib/bin

HTTPD=./httpd.py

CC32=${NACL_SDK_BIN_DIR}/i686-nacl-gcc
CC64=${NACL_SDK_BIN_DIR}/x86_64-nacl-gcc
STRIP32=${NACL_SDK_BIN_DIR}/i686-nacl-strip
STRIP64=${NACL_SDK_BIN_DIR}/x86_64-nacl-strip


RODATA_START=0x4800000


#CFLAGS=-O2 -Wall -Werror -DRODATA_START=${RODATA_START}
CFLAGS=-O2 -Wall -DRODATA_START=${RODATA_START}
LFLAGS=${CFLAGS} -Wl,--section-start,.rodata=${RODATA_START}
LIBS=-lppapi -lpthread


CFLAGS_X86_32=-DARCH_X86_32 ${CFLAGS}
CFLAGS_X86_64=-DARCH_X86_64 ${CFLAGS}
LFLAGS_X86_32=${LFLAGS}
LFLAGS_X86_64=${LFLAGS}


OUT=out
DEPLOY=$(OUT)/deploy
DEPLOY_STATIC=$(DEPLOY)/static
OBJ=$(OUT)/obj


NACLFORTH32=$(DEPLOY_STATIC)/naclforth_x86-32.nexe
NACLFORTH64=$(DEPLOY_STATIC)/naclforth_x86-64.nexe


all: naclforth_web


$(NACLFORTH32): $(OBJ)/naclforth_x86-32.o $(DEPLOY_STATIC)
	${CC32} -o $@.unstripped $< ${LFLAGS_X86_32} ${LIBS}
	${STRIP32} $@.unstripped -o $@

$(NACLFORTH64): $(OBJ)/naclforth_x86-64.o $(DEPLOY_STATIC)
	${CC64} -o $@.unstripped $< ${LFLAGS_X86_64} ${LIBS}
	${STRIP64} $@.unstripped -o $@

$(OBJ)/naclforth_x86-32.o: naclforth.c $(OBJ)
	${CC32} -o $@ -c $< ${CFLAGS_X86_32}

$(OBJ)/naclforth_x86-64.o: naclforth.c $(OBJ)
	${CC64} -o $@ -c $< ${CFLAGS_X86_64}

$(DEPLOY)/%: web/%
	mkdir -p $(@D)
	cp $< $@

naclforth_web: $(NACLFORTH32) \
               $(NACLFORTH64) \
               $(DEPLOY_STATIC)/naclforth.boot \
               $(DEPLOY_STATIC)/nacl4th.ico \
               $(DEPLOY_STATIC)/nacl4th.png \
               $(DEPLOY)/naclforth.py \
               $(DEPLOY)/app.yaml \
               $(DEPLOY)/index.yaml \
               $(DEPLOY)/naclforth.html

$(DEPLOY_STATIC)/naclforth.boot: naclforth.boot
	cp $< $@

$(DEPLOY)/naclforth.html: naclforth.html
	cp $< $@

$(OUT) $(DEPLOY) $(OBJ) $(DEPLOY_STATIC):
	mkdir -p $@

deploy: naclforth_web
	appcfg.py update web

httpd: httpdstop
	bash -c '${HTTPD} & echo $$! >httpd.pid'

httpdstop:
	kill `cat httpd.pid` ; rm httpd.pid || true

grab:
	curl 'https://naclforth.appspot.com/read?owner=0&section=0&index=0&count=4' -o naclforth.boot

start: httpd chrome

stop: httpdstop chromestop

clean:
	rm -rf $(OUT) `find ./ -name "*~"`