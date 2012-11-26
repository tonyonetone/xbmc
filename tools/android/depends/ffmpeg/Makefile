include ../Makefile.include
DEPS= ../Makefile.include Makefile

# lib name, version
LIBNAME=ffmpeg

LIBDYLIB=$(PLATFORM)/lib/.libs/lib$(LIBNAME).a

replace1= \
	$(RPL) -e "lib$(2).so.$(3)" "lib$(2).so\x00\x00" $(PREFIX)/lib/lib$(1).so ; 
 
replace2= \
	$(RPL) -e "lib$(2).so.$(3)" "lib$(2).so\x00\x00\x00" $(PREFIX)/lib/lib$(1).so ; 

process = \
	rm -f $(PREFIX)/lib/lib$(1).* ; \
	cp -f $(PLATFORM)/lib$(1)/lib$(1).so $(PREFIX)/lib  ; \
	$(call replace1,$(1),avfilter,3)  \
	$(call replace2,$(1),avformat,54)  \
	$(call replace2,$(1),avcodec,54)  \
	$(call replace2,$(1),postproc,52)  \
	$(call replace2,$(1),avcodec,54)  \
	$(call replace1,$(1),swscale,2)  \
	$(call replace1,$(1),swresample,0)  \
	$(call replace2,$(1),avutil,52)  \
	$(call replace2,$(1),avdevice,54)  \
	$(READELF) --dynamic $(PREFIX)/lib/lib$(1).so | grep ibrary ;


all: .installed-$(PLATFORM)


$(LIBDYLIB):
	$(MAKE) -C $(PLATFORM)

.installed-$(PLATFORM): $(LIBDYLIB)
	$(MAKE) -C $(PLATFORM) install
	$(call process,avfilter)
	$(call process,avformat)
	$(call process,avcodec)
	$(call process,postproc)
	$(call process,avcodec)
	$(call process,swscale)
	$(call process,swresample)
	$(call process,avutil)
	$(call process,avdevice)
	touch $@

clean:
	$(MAKE) -C $(PLATFORM) clean
	rm -f .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)

