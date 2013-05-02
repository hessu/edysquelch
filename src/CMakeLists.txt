add_executable(edysquelch edysquelch.c cfg.c crc32.c hlog.c input.c receiver.c
	splay.c spsymbol.c
	cJSON.c
	cache.c cfgfile.c filter.c hmalloc.c rwlock.c fingerpori.c)

target_link_libraries(edysquelch pthread asound)

install(TARGETS edysquelch DESTINATION bin)
install (FILES edysquelch.conf DESTINATION /etc)

