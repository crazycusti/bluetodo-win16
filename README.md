# BlueTodo Win16

Win16-Client für das BlueTodo-TCP-Protokoll.

## Stand

- nativer Win16-Dialogclient
- Todos und Tasks anzeigen, bearbeiten, archivieren und wiederherstellen
- Client-Update über das BlueTodo-Protokoll

## Build

- Open Watcom v2 installieren
- `WATCOM=/pfad/zu/openwatcom ./build.sh`
- alternativ direkt `wmake`

Ausgabe:

- `bluetodo-win16.exe`
- `btupdt16.exe`

## Hinweise

- für Verbindungen wird eine WinSock-1.1-Umgebung benötigt
- aktuell werden nur numerische IPv4-Adressen unterstützt
- Verbindungsdaten liegen in `BLUETODO.INI`

## Lizenz

MIT, siehe `LICENSE`.

Mit Hilfe von codex-cli ❤️ erstellt, danke!
