# R SMB

[English](README.md) · [한국어](README.ko.md) · **日本語** · [Italiano](README.it.md) · [Français](README.fr.md)

R SMB を入れると、RenkuOS/HaikuOS から同じネットワークにある
Windows PC、Mac、NAS の共有フォルダーを開けるようになります。

![Tracker で見たネットワーク上のコンピューター](docs/images/tracker.png)

![ネットワーク設定と R SMB の設定ウィンドウ](docs/images/settings.png)

## FuseSMB との比較

FuseSMB (`fusesmb_haiku`) は、RenkuOS/HaikuOS で共有フォルダーを開くための
以前からある方法です。

| | R SMB | FuseSMB |
| --- | --- | --- |
| 追加で入れるもの | なし | Samba パッケージが必要 |
| コンピューターの検索 | ネットワークを直接探し、名前で表示 | Windows のワークグループ (NetBIOS) で探し、ワークグループごとに表示 |
| Mac | いつもの Mac のユーザー名とパスワードでログイン | Samba のログイン方式に従う |
| 遅いコンピューターがあるとき | ほかのコンピューターはそのまま動く | すべてのコンピューターが 1 つのロックを共有するため、一緒に待つ |
| 保存 | ファイルがサーバーに届いたことを確認できる (fsync) | fsync なし |
| 最終更新 | 2026 年 | 2022 年 |

## インストール

1. **ターミナル** を開きます。画面右上の羽根のメニューを押し、
   **アプリケーション > ターミナル** を押します。
2. コンピューターの種類を確かめます。ターミナルに `uname -m` と入力して
   **Enter** を押してください。`x86_64` は 64 ビット、`BePC` は 32 ビット、
   `arm64` は arm64 です。
3. お使いのコンピューターに合う 3 行をコピーしてターミナルに貼り付け、
   **Enter** を押します。

   **64 ビット版 RenkuOS/HaikuOS:**

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_64
   pkgman refresh
   pkgman install rsmb
   ```

   **32 ビット版 RenkuOS/HaikuOS:**

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

4. ターミナルに何か聞かれたら **y** を入力して **Enter** を押します。
5. 終わるとデスクトップに **R SMB** のアイコンが現れます。ダブルクリックすると
   ネットワーク上のコンピューターが表示されます。

## ライセンス

MIT。libsmb2 (GNU LGPL 2.1) と Haiku の一部 (MIT) を含みます。

## AI に関する開示

このプログラムは Claude と共に書かれました。
