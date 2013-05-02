add_executable(gnuais ais.c ais.c cfg.c crc32.c hlog.c input.c receiver.c splay.c cache.c cfgfile.c filter.c hmalloc.c rwlock.c spsymbol.c) 

target_link_libraries(gnuais pthread asound)

install(TARGETS gnuais DESTINATION bin)
install (FILES gnuais.conf DESTINATION /etc)

