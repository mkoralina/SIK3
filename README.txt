Werjsa 0.0:

- dziala przesylanie dzwieku KLIENT -> SERWER
- wywala sie gdzies w mikserze





Komentarze własne:

do plikow MP3:
sudo apt-get install libsox-fmt-mp3

valgrind -v --leak-check=full --track-origins=yes ./server

./server | aplay -t raw -f cd -B 5000 -v -D sysdefault -

Nagle algorithm
http://www.unixguide.net/network/socketfaq/2.16.shtml


Your callback function must not modify any of the events that it receives, or add or remove any events to the event base, or otherwise modify any event associated with the event base, or undefined behavior can occur, up to or including crashes and heap-smashing.
do set_keepalive_event !!!
