remoteClient:	remoteClient.o
				g++ remoteClient.o -o remoteClient

dataServer:		dataServer.o
				g++ dataServer.o -o dataServer -lpthread

remoteClient.o:	remoteClient.cpp
				g++ -c remoteClient.cpp -o remoteClient.o

dataServer.o:	dataServer.cpp
				g++ -c dataServer.cpp -o dataServer.o -lpthread

all:			remoteClient dataServer

clean:
				rm -f dataServer.o remoteClient.o dataServer remoteClient
