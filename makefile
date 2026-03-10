TARGET = bluetodo-win16.exe
UPDATER = btupdt16.exe
OBJS = main.o proto.o proto_wire.o
UPDATER_OBJS = updater.o
CFLAGS = -q -bt=windows -ml -zWs -3

.c.o:
	wcc $(CFLAGS) $<

all: $(TARGET) $(UPDATER) .SYMBOLIC

bluetodo.res: bluetodo.rc resource.h
	wrc -q -bt=windows -r bluetodo.rc

$(TARGET): $(OBJS) bluetodo.res
	wlink option quiet system windows name $(TARGET) file main.o file proto.o file proto_wire.o
	wrc -q -bt=windows bluetodo.res $(TARGET)

$(UPDATER): $(UPDATER_OBJS)
	wlink option quiet system windows name $(UPDATER) file updater.o

clean: .SYMBOLIC
	rm -f $(OBJS) $(UPDATER_OBJS) bluetodo.res $(TARGET) $(UPDATER) *.map *.err
