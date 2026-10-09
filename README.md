# R SMB

**English** · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md)

R SMB lets Haiku open the shared folders of Windows PCs, Macs and NAS
devices on your network.

## Install

1. Open **Terminal**: click the feather menu at the top right of the screen,
   then **Applications > Terminal**.
2. Copy the three lines for your computer, paste them into Terminal, and
   press **Enter**.

   **Most PCs (32-bit Haiku):**

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

3. If Terminal asks a question, type **y** and press **Enter**.
4. When it finishes, an **R SMB** icon appears on the Desktop. Double-click
   it to see the computers on your network.

64-bit Haiku (x86_64) is not supported yet.

## License

MIT. Includes libsmb2 (GNU LGPL 2.1) and parts of Haiku (MIT).
