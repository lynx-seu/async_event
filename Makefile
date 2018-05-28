
target = main
obj = eventloop.o main.o
CFLAGS = -std=c++11

all:$(target)

$(target):$(obj)
	@$(CC) $(LDFLAG) $^ -o $@

%.o:%.cpp
	@$(CC) $(CFLAGS) $< -o $@
