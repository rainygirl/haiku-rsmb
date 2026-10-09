# R SMB

**English** · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md)

R SMB lets RenkuOS/HaikuOS open the shared folders of Windows PCs, Macs and
NAS devices on your network.

![Computers on the network in Tracker](docs/images/tracker.png)

![Network preferences and R SMB settings](docs/images/settings.png)

## Install

1. Open **Terminal**: click the feather menu at the top right of the screen,
   then **Applications > Terminal**.
2. Find out which kind your computer is: type `uname -m` in Terminal and
   press **Enter**. `x86_64` means 64-bit, `BePC` means 32-bit, `arm64`
   means arm64.
3. Copy the three lines for your computer, paste them into Terminal, and
   press **Enter**.

   **64-bit RenkuOS/HaikuOS:**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_64
   pkgman refresh
   pkgman install rsmb
   ```

   **32-bit RenkuOS/HaikuOS:**

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

4. If Terminal asks a question, type **y** and press **Enter**.
5. When it finishes, an **R SMB** icon appears on the Desktop. Double-click
   it to see the computers on your network.

## License

MIT. Includes libsmb2 (GNU LGPL 2.1) and parts of Haiku (MIT).
