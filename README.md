# BlueTodo Win16

Win16-Client fuer das BlueTodo-TCP-Protokoll.
Das Projekt ist in C fuer Open Watcom v2 umgesetzt und richtet sich an Windows 3.x.

## Stand

- nativer Win16-Dialogclient
- Todos und Tasks anzeigen, bearbeiten, archivieren und wiederherstellen
- Client-Update ueber das BlueTodo-Protokoll
- keine Delphi-Abhaengigkeit

## Build

- Open Watcom v2 installieren
- `WATCOM=/pfad/zu/openwatcom ./build.sh`
- alternativ direkt `wmake`

Ausgabe:

- `bluetodo-win16.exe`
- `btupdt16.exe`

## Hinweise

- fuer Verbindungen wird eine WinSock-1.1-Umgebung benoetigt
- aktuell werden nur numerische IPv4-Adressen unterstuetzt
- Verbindungsdaten liegen in `BLUETODO.INI`

## Lizenz

MIT, siehe `LICENSE`.
