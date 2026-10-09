# R SMB

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Italiano** · [Français](README.fr.md)

R SMB permette a Haiku di aprire le cartelle condivise di PC Windows, Mac e
dispositivi NAS della tua rete.

![I computer della rete in Tracker](docs/images/tracker.png)

![Preferenze di rete e impostazioni di R SMB](docs/images/settings.png)

## Installazione

1. Apri il **Terminale**: fai clic sul menu con la piuma in alto a destra,
   poi su **Applicazioni > Terminale**.
2. Copia le tre righe adatte al tuo computer, incollale nel Terminale e premi
   **Invio**.

   **La maggior parte dei PC (Haiku a 32 bit):**

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

3. Se il Terminale fa una domanda, scrivi **y** e premi **Invio**.
4. Alla fine compare l'icona **R SMB** sulla Scrivania. Fai doppio clic per
   vedere i computer della tua rete.

Haiku a 64 bit (x86_64) non è ancora supportato.

## Licenza

MIT. Include libsmb2 (GNU LGPL 2.1) e parti di Haiku (MIT).
