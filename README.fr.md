# R SMB

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · **Français**

R SMB permet à RenkuOS/HaikuOS d'ouvrir les dossiers partagés des PC Windows,
des Mac et des NAS de votre réseau.

![Les ordinateurs du réseau dans Tracker](docs/images/tracker.png)

![Préférences Réseau et réglages de R SMB](docs/images/settings.png)

## Comparaison avec FuseSMB

FuseSMB (`fusesmb_haiku`) est l'ancienne façon d'ouvrir les dossiers partagés
sous RenkuOS/HaikuOS.

| | R SMB | FuseSMB |
| --- | --- | --- |
| Logiciel en plus | Rien à installer | Nécessite le paquet Samba |
| Recherche des ordinateurs | Cherche sur le réseau et affiche chaque ordinateur par son nom | Liste les ordinateurs par groupes de travail Windows (NetBIOS), classés par groupe |
| Mac | Connexion avec le nom d'utilisateur et le mot de passe habituels du Mac | Utilise la connexion de Samba |
| Un ordinateur lent | Les autres ordinateurs continuent de fonctionner | Tous les ordinateurs attendent, car ils partagent un seul verrou |
| Enregistrement | Peut confirmer que le fichier est arrivé sur le serveur (fsync) | Pas de fsync |
| Dernière modification | 2026 | 2022 |

## Installation

1. Ouvrez le **Terminal** : cliquez sur le menu en forme de plume en haut à
   droite de l'écran, puis sur **Applications > Terminal**.
2. Vérifiez le type de votre ordinateur : tapez `uname -m` dans le Terminal
   et appuyez sur **Entrée**. `x86_64` signifie 64 bits, `BePC` 32 bits,
   `arm64` arm64.
3. Copiez les trois lignes qui correspondent à votre ordinateur, collez-les
   dans le Terminal et appuyez sur **Entrée**.

   **RenkuOS/HaikuOS 64 bits :**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_64
   pkgman refresh
   pkgman install rsmb
   ```

   **RenkuOS/HaikuOS 32 bits :**

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

4. Si le Terminal pose une question, tapez **y** et appuyez sur **Entrée**.
5. À la fin, une icône **R SMB** apparaît sur le Bureau. Double-cliquez dessus
   pour voir les ordinateurs de votre réseau.

## Licence

MIT. Inclut libsmb2 (GNU LGPL 2.1) et des parties de Haiku (MIT).

## Utilisation de l'IA

Ce programme a été écrit avec Claude.
