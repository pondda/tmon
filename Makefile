make: tmon.cpp
	g++ tmon.cpp -std=c++17 -lncursesw -pthread -o tmon

install: tmon
	cp tmon /bin/

uninstall:
	rm /bin/tmon

clean: tmon
	rm tmon

run: tmon
	./tmon
