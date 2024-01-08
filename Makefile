make: tmon.cpp
	g++ tmon.cpp -lncursesw -pthread -o tmon

install: tmon
	cp tmon /bin/

uninstall:
	rm /bin/tmon

clean: tmon
	rm tmon

run: tmon
	./tmon
