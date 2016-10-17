default:
	gcc -g main.c -o main `pkg-config fuse --cflags --libs`
	gcc create_container.c -o create_container
clean:
	rm main
	rm create_container

