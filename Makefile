CXX = g++
CXXFLAGS = -Wall -g
OBJS = command.o tokenizer.o
TARGET = myshell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cc command.h tokenizer.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) shell.log

.PHONY: all clean
