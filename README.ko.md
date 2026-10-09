# R SMB

[English](README.md) · **한국어** · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md)

R SMB를 설치하면 RenkuOS/HaikuOS에서 같은 네트워크에 있는
Windows PC, Mac, NAS의 공유 폴더를 열 수 있습니다.

![Tracker에서 본 네트워크의 컴퓨터들](docs/images/tracker.png)

![네트워크 설정과 R SMB 설정 창](docs/images/settings.png)

## FuseSMB와 비교

FuseSMB(`fusesmb_haiku`)는 RenkuOS/HaikuOS에서 공유 폴더를 열던 예전 방법입니다.

| | R SMB | FuseSMB |
| --- | --- | --- |
| 따로 설치할 것 | 없음 | Samba 패키지가 필요함 |
| 컴퓨터 찾기 | 네트워크를 직접 찾아 컴퓨터를 이름으로 보여 줌 | Windows 작업 그룹(NetBIOS)으로 찾아 작업 그룹별로 보여 줌 |
| Mac | 평소 쓰는 Mac 사용자 이름과 비밀번호로 로그인 | Samba의 로그인 방식을 따름 |
| 느린 컴퓨터가 있을 때 | 다른 컴퓨터는 그대로 동작 | 모든 컴퓨터가 잠금 하나를 같이 써서 함께 기다림 |
| 저장 | 파일이 서버에 저장됐는지 확인 가능(fsync) | fsync 없음 |
| 마지막 변경 | 2026년 | 2022년 |

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

## AI 고지

이 프로그램은 Claude와 함께 작성되었습니다.
