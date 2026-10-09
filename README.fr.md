# R SMB

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · **Français**

R SMB permet à Haiku d'ouvrir les dossiers partagés des PC Windows, des Mac
et des NAS de votre réseau.

## Installation

1. Ouvrez le **Terminal** : cliquez sur le menu en forme de plume en haut à
   droite de l'écran, puis sur **Applications > Terminal**.
2. Copiez les trois lignes qui correspondent à votre ordinateur, collez-les
   dans le Terminal et appuyez sur **Entrée**.

   **La plupart des PC (Haiku 32 bits) :**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rsmb_x86
   ```

   **arm64 (RENKU) :**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64
   pkgman refresh
   pkgman install rsmb
   ```

3. Si le Terminal pose une question, tapez **y** et appuyez sur **Entrée**.
4. À la fin, une icône **R SMB** apparaît sur le Bureau. Double-cliquez dessus
   pour voir les ordinateurs de votre réseau.

Haiku 64 bits (x86_64) n'est pas encore pris en charge.

## Licence

MIT. Inclut libsmb2 (GNU LGPL 2.1) et des parties de Haiku (MIT).
