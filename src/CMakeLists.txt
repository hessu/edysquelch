add_executable(edysquelch edysquelch.c cfg.c hlog.c input.c receiver.c
	out_json.c cJSON.c
	cfgfile.c filter.c hmalloc.c rwlock.c fingerpori.c)

target_link_libraries(edysquelch pthread asound curl)

install(TARGETS edysquelch DESTINATION bin)
install (FILES edysquelch.conf DESTINATION /etc)

