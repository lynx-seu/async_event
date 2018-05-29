
target = main
obj = eventloop.o main.o
CC=g++
CFLAGS = -std=c++11

all:$(target)

$(target):$(obj)
	$(CC) $(LDFLAG) $^ -o $@

%.o:%.cpp
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf $(target) $(obj)
