# R SMB

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Italiano** · [Français](README.fr.md)

R SMB permette a RenkuOS/HaikuOS di aprire le cartelle condivise di PC
Windows, Mac e dispositivi NAS della tua rete.

![I computer della rete in Tracker](docs/images/tracker.png)

![Preferenze di rete e impostazioni di R SMB](docs/images/settings.png)

## Confronto con FuseSMB

FuseSMB (`fusesmb_haiku`) è il modo più vecchio per aprire le cartelle
condivise su RenkuOS/HaikuOS.

| | R SMB | FuseSMB |
| --- | --- | --- |
| Software in più | Nulla da installare | Serve il pacchetto Samba |
| Ricerca dei computer | Cerca nella rete e mostra ogni computer per nome | Elenca i computer tramite i gruppi di lavoro Windows (NetBIOS), divisi per gruppo |
| Mac | Accesso con il normale nome utente e password del Mac | Usa l'accesso di Samba |
| Un computer lento | Gli altri computer continuano a funzionare | Tutti i computer aspettano, perché condividono un unico blocco |
| Salvataggio | Può confermare che il file è arrivato al server (fsync) | Niente fsync |
| Ultima modifica | 2026 | 2022 |

## Installazione

1. Apri il **Terminale**: fai clic sul menu con la piuma in alto a destra,
   poi su **Applicazioni > Terminale**.
2. Scopri che tipo di computer hai: scrivi `uname -m` nel Terminale e premi
   **Invio**. `x86_64` significa 64 bit, `BePC` 32 bit, `arm64` arm64.
3. Copia le tre righe adatte al tuo computer, incollale nel Terminale e premi
   **Invio**.

   **RenkuOS/HaikuOS a 64 bit:**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_64
   pkgman refresh
   pkgman install rsmb
   ```

   **RenkuOS/HaikuOS a 32 bit:**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rsmb_x86
   ```

   **arm64 (RENKU):**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64
   pkgman refresh
   pkgman install rsmb
   ```

4. Se il Terminale fa una domanda, scrivi **y** e premi **Invio**.
5. Alla fine compare l'icona **R SMB** sulla Scrivania. Fai doppio clic per
   vedere i computer della tua rete.

## Licenza

MIT. Include libsmb2 (GNU LGPL 2.1) e parti di Haiku (MIT).

## Dichiarazione sull'uso dell'IA

Questo programma e' stato scritto con Claude.
