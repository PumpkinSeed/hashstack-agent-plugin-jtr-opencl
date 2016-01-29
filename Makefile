
all:
	cd src && \
	./configure --disable-cuda --disable-pcap && \
	make -j
	find run/ -type l -exec rm -vf '{}' \;
	find run/ -type f -executable ! -name john -exec rm -vf '{}' \;

install:
	mkdir -p $(DESTDIR)/opt/hashstack/programs/JohnTheRipper/
	mkdir -p $(DESTDIR)/opt/hashstack/agent/plugins/
	cp JohnTheRipper.json $(DESTDIR)/opt/hashstack/agent/plugins/
	cd run/ && cp -R * $(DESTDIR)/opt/hashstack/programs/JohnTheRipper/

clean:
	cd src/ && make distclean ; true

