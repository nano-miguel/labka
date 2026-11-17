# Rubic-Labka

Gestor visual de red y SSH para Linux.

## Que hace

- Mapa visual de tu red local con los dispositivos detectados.
- Credenciales SSH guardadas cifradas (AES-256-GCM, clave derivada con Argon2id).
- Terminal SSH, explorador de archivos por SFTP y escritorio remoto VNC con un clic.
- Reconexion automatica si un equipo se cae.
- Configura SSH y firewall en el equipo remoto solo (Debian, Fedora, Arch, openSUSE).

## Requisitos

- Linux con Qt6, libssh y Botan 3.
- openssh-server en los equipos remotos. La app puede instalarlo si falta.

## Compilar

    cmake -B build
    cmake --build build -j$(nproc)
    ./build/rubic-labka

## Paquete

    cpack

Genera un .deb instalable.

## Estado

Alpha. Funciona en mi red casera (una Raspberry con Fedora y varias PCs).
Puede romperse. Por eso esta aqui.

## Feedback

No hay donaciones. Solo revision y comentarios.
Abre un issue o deja un comentario con:

- que se rompio y como reproducirlo
- que parte del codigo harias mejor
- que sobra o que falta

## Licencia

GPLv3.
