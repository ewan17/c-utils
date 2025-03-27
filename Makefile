CC	= gcc
AR	= ar
W = -W -Wall -g

LIB = lib
BIN = bin
TARGET = $(LIB)/libutils.a

.PHONY: clean bin

all:	bin	$(TARGET)

clean:
	rm -rf $(BIN) $(LIB) $(TARGET)	

bin:
	@mkdir -p $(BIN)
	@mkdir -p $(LIB)

%.o:	%.c
	$(CC) -c $< -o $(BIN)/$@

$(TARGET):	utilities.o 
	$(AR) rs $@ $(BIN)/$^