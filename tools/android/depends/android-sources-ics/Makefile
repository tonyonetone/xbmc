include ../Makefile.include

ANDROID_VERSION=ics
ANDROID_SOURCE=android-source
ANDROID_LIBS=android-libs
ANDROID_LIBS_ARCHIVE=cm-9.1.0-galaxys2.zip

all: .installed-$(PLATFORM)

.installed-$(PLATFORM): $(ANDROID_SOURCE) $(ANDROID_LIBS)
	mkdir -p $(PREFIX)/opt
	ln -s $(CURDIR)/$(ANDROID_SOURCE) $(PREFIX)/opt/$(ANDROID_SOURCE)
	ln -s $(CURDIR)/$(ANDROID_LIBS) $(PREFIX)/opt/$(ANDROID_LIBS)
	touch $@

$(ANDROID_SOURCE):
	echo "Fetching Android system headers"
	git clone --depth=1 --branch $(ANDROID_VERSION)-release git://github.com/CyanogenMod/android_frameworks_base.git $(ANDROID_SOURCE)/frameworks/base
	git clone --depth=1 --branch $(ANDROID_VERSION)-release git://github.com/CyanogenMod/android_system_core.git $(ANDROID_SOURCE)/system/core
	git clone --depth=1 --branch $(ANDROID_VERSION)-release git://github.com/CyanogenMod/android_hardware_libhardware.git $(ANDROID_SOURCE)/libhardware

$(ANDROID_LIBS): $(ANDROID_LIBS_ARCHIVE)
	echo "Extracting Android libs"
	unzip $(ANDROID_LIBS_ARCHIVE) system/lib/*
	mv system/lib $(ANDROID_LIBS)
	rm $(ANDROID_LIBS)/libstdc++.so
	rmdir system

$(ANDROID_LIBS_ARCHIVE):
	echo "Fetching Android libs"
	wget "http://get.cm/get/jenkins/7078/$(ANDROID_LIBS_ARCHIVE)"

clean:
	rm -rf .installed-$(PLATFORM) $(ANDROID_SOURCE) $(ANDROID_LIBS) $(ANDROID_LIBS_ARCHIVE)

distclean: clean
