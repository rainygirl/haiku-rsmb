# R SMB

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Italiano** · [Français](README.fr.md)

R SMB permette a RenkuOS/HaikuOS di aprire le cartelle condivise di PC
Windows, Mac e dispositivi NAS della tua rete.

![I computer della rete in Tracker](docs/images/tracker.png)

![Preferenze di rete e impostazioni di R SMB](docs/images/settings.png)

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
