all: WTF.c helper.o WTFserver.c
				gcc -fsanitize=address -o WTF WTF.c helper.o -lssl -lcrypto -lm
				gcc -fsanitize=address -o WTFserver WTFserver.c helper.o -lpthread -DMUTEX -lm -lssl -lcrypto

helper.o: helper.c
				gcc -c helper.c -lm -lssl -lcrypto

clean:
				rm -f WTF
				rm -f WTFserver
				rm -f helper.o
				rm -rf .server_directory
				rm -f .configure
