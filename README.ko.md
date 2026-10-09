# R SMB

[English](README.md) · **한국어** · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md)

R SMB를 설치하면 RenkuOS/HaikuOS에서 같은 네트워크에 있는
Windows PC, Mac, NAS의 공유 폴더를 열 수 있습니다.

![Tracker에서 본 네트워크의 컴퓨터들](docs/images/tracker.png)

![네트워크 설정과 R SMB 설정 창](docs/images/settings.png)

## 설치

1. **터미널**을 엽니다. 화면 오른쪽 위의 깃털 메뉴를 누르고
   **응용 프로그램 > 터미널**을 누릅니다.
2. 내 컴퓨터의 종류를 확인합니다. 터미널에 `uname -m`을 입력하고 **Enter**를
   누르세요. `x86_64`는 64비트, `BePC`는 32비트, `arm64`는 arm64입니다.
3. 내 컴퓨터에 맞는 세 줄을 복사해 터미널에 붙여 넣고 **Enter**를 누릅니다.

   **64비트 RenkuOS/HaikuOS:**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_64
   pkgman refresh
   pkgman install rsmb
   ```

   **32비트 RenkuOS/HaikuOS:**

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

4. 터미널이 무언가를 물어보면 **y**를 입력하고 **Enter**를 누릅니다.
5. 설치가 끝나면 데스크탑에 **R SMB** 아이콘이 생깁니다. 두 번 누르면
   네트워크의 컴퓨터들이 보입니다.

## 라이선스

MIT. libsmb2(GNU LGPL 2.1)와 Haiku의 일부(MIT)가 포함되어 있습니다.
