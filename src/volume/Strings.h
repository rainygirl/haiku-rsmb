// User-visible strings: English, Japanese, Korean, Italian, French.
// C++98, shared by the settings app and the gcc2-compatible Network add-on.
// Terms follow Haiku's own catalogs (Network services, Tracker).
#pragma once
#include <LocaleRoster.h>
#include <Message.h>
#include <String.h>
#include <string.h>

namespace rsmb {
struct Translation {
    const char* en;
    const char* ja;
    const char* ko;
    const char* it;
    const char* fr;
};
static const Translation kTranslations[] = {
    // Settings window
    { "R SMB settings", "R SMB 設定", "R SMB 설정", "Impostazioni di R SMB", "Réglages de R SMB" },
    { "Servers", "サーバー", "서버", "Server", "Serveurs" },
    { "Login", "ログイン", "로그인", "Credenziali", "Identifiants" },
    { "Server:", "サーバー:", "서버:", "Server:", "Serveur :" },
    { "User:", "ユーザー名:", "사용자 이름:", "Nome utente:", "Utilisateur :" },
    { "Password:", "パスワード:", "비밀번호:", "Password:", "Mot de passe :" },
    { "Domain:", "ドメイン:", "도메인:", "Dominio:", "Domaine :" },
    { "Name or IP address, or smb://server/share", "名前または IP アドレス、または smb://サーバー/共有",
        "이름 또는 IP 주소, 또는 smb://서버/공유", "Nome o indirizzo IP, oppure smb://server/condivisione",
        "Nom ou adresse IP, ou smb://serveur/partage" },
    { "Leave blank to connect as guest", "空欄にするとゲストとして接続します", "비워 두면 게스트로 연결합니다",
        "Lascia vuoto per connetterti come ospite", "Laissez vide pour vous connecter en invité" },
    { "Find servers", "サーバーを検索", "서버 찾기", "Cerca server", "Rechercher des serveurs" },
    { "Save login", "ログイン情報を保存", "로그인 저장", "Salva credenziali", "Enregistrer" },
    { "Passwords are kept in Haiku KeyStore.", "パスワードは Haiku KeyStore に保存されます。",
        "비밀번호는 Haiku KeyStore에 보관됩니다.", "Le password sono conservate nel KeyStore di Haiku.",
        "Les mots de passe sont conservés dans le KeyStore de Haiku." },
    { "Select a server to enter its login, or add one.",
        "サーバーを選んでログイン情報を入力するか、新しく追加してください。",
        "서버를 선택해 로그인 정보를 입력하거나 새로 추가하세요.",
        "Seleziona un server per inserire le credenziali, oppure aggiungine uno.",
        "Sélectionnez un serveur pour saisir ses identifiants, ou ajoutez-en un." },
    { "Searching the network…", "ネットワークを検索しています…", "네트워크를 검색하는 중…", "Ricerca nella rete…",
        "Recherche sur le réseau…" },
    { "Found 1 server.", "サーバーが 1 台見つかりました。", "서버 1대를 찾았습니다.", "Trovato 1 server.",
        "1 serveur trouvé." },
    { "Found %d servers.", "サーバーが %d 台見つかりました。", "서버 %d대를 찾았습니다.", "Trovati %d server.",
        "%d serveurs trouvés." },
    { "1 needs a login: select it and enter it.", "1 台はログインが必要です。選択して入力してください。",
        "1대는 로그인이 필요합니다. 선택해서 입력하세요.",
        "1 richiede le credenziali: selezionalo e inseriscile.",
        "1 nécessite des identifiants : sélectionnez-le et saisissez-les." },
    { "%d need a login: select one and enter it.", "%d 台はログインが必要です。選択して入力してください。",
        "%d대는 로그인이 필요합니다. 선택해서 입력하세요.",
        "%d richiedono le credenziali: selezionane uno e inseriscile.",
        "%d nécessitent des identifiants : sélectionnez-en un et saisissez-les." },
    { "No servers on this network. Enter a server address and save it.",
        "このネットワークにサーバーはありません。サーバーのアドレスを入力して保存してください。",
        "이 네트워크에서 서버를 찾지 못했습니다. 서버 주소를 입력해 저장하세요.",
        "Nessun server in questa rete. Inserisci l'indirizzo di un server e salvalo.",
        "Aucun serveur sur ce réseau. Saisissez l'adresse d'un serveur et enregistrez-la." },
    { "Cannot save discovered server.", "見つかったサーバーを保存できません。", "찾은 서버를 저장할 수 없습니다.",
        "Impossibile salvare il server trovato.", "Impossible d'enregistrer le serveur trouvé." },
    { "Enter a server or share, without a subfolder.", "サブフォルダーを含めず、サーバーまたは共有を入力してください。",
        "하위 폴더 없이 서버나 공유만 입력하세요.", "Inserisci un server o una condivisione, senza sottocartelle.",
        "Saisissez un serveur ou un partage, sans sous-dossier." },
    { "Could not store the password in Haiku KeyStore.", "パスワードを Haiku KeyStore に保存できませんでした。",
        "비밀번호를 Haiku KeyStore에 저장하지 못했습니다.", "Impossibile salvare la password nel KeyStore di Haiku.",
        "Impossible d'enregistrer le mot de passe dans le KeyStore de Haiku." },
    { "Could not save the login.", "ログイン情報を保存できませんでした。", "로그인 정보를 저장하지 못했습니다.",
        "Impossibile salvare le credenziali.", "Impossible d'enregistrer les identifiants." },
    { "Saved. Open the server in the R SMB volume on the Desktop.",
        "保存しました。デスクトップの R SMB ボリュームからサーバーを開いてください。",
        "저장했습니다. 데스크탑의 R SMB 볼륨에서 서버를 여세요.",
        "Salvato. Apri il server dal volume R SMB sulla Scrivania.",
        "Enregistré. Ouvrez le serveur depuis le volume R SMB du Bureau." },
    // Address and network errors shown in the status line
    { "Invalid percent escape in SMB address.", "SMB アドレスのパーセントエンコードが正しくありません。",
        "SMB 주소의 퍼센트 인코딩이 올바르지 않습니다.", "Codifica percentuale non valida nell'indirizzo SMB.",
        "Encodage pourcent non valide dans l'adresse SMB." },
    { "Invalid path component.", "パスに無効な要素があります。", "경로에 올바르지 않은 항목이 있습니다.",
        "Componente del percorso non valido.", "Élément de chemin non valide." },
    { "Enter a server name or IP address. Use the login fields for credentials.",
        "サーバー名または IP アドレスを入力してください。認証情報はログイン欄に入力します。",
        "서버 이름이나 IP 주소를 입력하세요. 계정 정보는 로그인 칸에 입력합니다.",
        "Inserisci il nome o l'indirizzo IP del server. Usa i campi delle credenziali per l'accesso.",
        "Saisissez le nom ou l'adresse IP du serveur. Utilisez les champs d'identification pour vous connecter." },
    { "Cannot read network interfaces.", "ネットワークインターフェースを読み取れません。",
        "네트워크 인터페이스를 읽을 수 없습니다.", "Impossibile leggere le interfacce di rete.",
        "Impossible de lire les interfaces réseau." },
    // Network preferences pane
    { "Settings…", "設定…", "설정…", "Impostazioni…", "Réglages…" },
    { "Enable", "有効にする", "활성화", "Abilita", "Activer" },
    { "Disable", "無効にする", "비활성화", "Disabilita", "Désactiver" },
    { "on", "オン", "켜기", "attivato", "allumé" },
    { "off", "オフ", "끄기", "disattivato", "éteint" },
    { "Shared folders of Windows PCs and NAS devices appear in the R SMB volume on the Desktop. "
      "Open them in Tracker like local folders.",
        "Windows PC や NAS の共有フォルダーが、デスクトップの R SMB ボリュームに表示されます。"
        "ローカルフォルダーと同じように Tracker で開けます。",
        "Windows PC와 NAS의 공유 폴더가 데스크탑의 R SMB 볼륨에 나타납니다. "
        "로컬 폴더처럼 Tracker에서 열 수 있습니다.",
        "Le cartelle condivise di PC Windows e dispositivi NAS compaiono nel volume R SMB sulla Scrivania. "
        "Aprile in Tracker come le cartelle locali.",
        "Les dossiers partagés des PC Windows et des NAS apparaissent dans le volume R SMB du Bureau. "
        "Ouvrez-les dans Tracker comme des dossiers locaux." },
    { "Connecting…", "接続しています…", "연결하는 중…", "Connessione…", "Connexion…" },
    { "Disconnecting…", "切断しています…", "연결을 끊는 중…", "Disconnessione…", "Déconnexion…" },
    { "R SMB could not be started. Reinstall R SMB.", "R SMB を起動できませんでした。R SMB を再インストールしてください。",
        "R SMB를 실행할 수 없습니다. R SMB를 다시 설치하세요.", "Impossibile avviare R SMB. Reinstalla R SMB.",
        "Impossible de démarrer R SMB. Réinstallez R SMB." },
    { "Could not connect. Reinstall R SMB if this persists.",
        "接続できませんでした。問題が続く場合は R SMB を再インストールしてください。",
        "연결하지 못했습니다. 계속되면 R SMB를 다시 설치하세요.",
        "Connessione non riuscita. Se il problema persiste, reinstalla R SMB.",
        "Connexion impossible. Si le problème persiste, réinstallez R SMB." },
    { "In use. Close R SMB files and windows, then try again.",
        "使用中です。R SMB のファイルとウィンドウを閉じてから、もう一度お試しください。",
        "사용 중입니다. R SMB 파일과 창을 닫고 다시 시도하세요.",
        "In uso. Chiudi i file e le finestre di R SMB, poi riprova.",
        "En cours d'utilisation. Fermez les fichiers et fenêtres R SMB, puis réessayez." },
};

// 0 = English. The first supported language in the user's Locale preferences wins.
inline int LanguageIndex()
{
    static int index = -1;
    if (index >= 0)
        return index;
    index = 0;
    BMessage languages;
    BLocaleRoster* roster = BLocaleRoster::Default();
    if (roster == NULL || roster->GetPreferredLanguages(&languages) != B_OK)
        return index;
    static const char* const codes[] = { "en", "ja", "ko", "it", "fr" };
    const char* language;
    for (int32 i = 0; languages.FindString("language", i, &language) == B_OK; ++i) {
        for (int c = 0; c < 5; ++c) {
            size_t n = strlen(codes[c]);
            if (strncmp(language, codes[c], n) == 0 && (language[n] == '\0' || language[n] == '_')) {
                index = c;
                return index;
            }
        }
    }
    return index;
}

// Returns the translation of an English string, or the string itself.
inline const char* T(const char* english)
{
    int language = LanguageIndex();
    if (language == 0 || english == NULL)
        return english;
    for (size_t i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); ++i) {
        const Translation& t = kTranslations[i];
        if (strcmp(t.en, english) == 0) {
            const char* all[] = { t.en, t.ja, t.ko, t.it, t.fr };
            return all[language];
        }
    }
    return english;
}

// T() for a message holding one %d count.
inline BString TCount(const char* one, const char* many, int count)
{
    BString text;
    if (count == 1)
        text = T(one);
    else
        text.SetToFormat(T(many), count);
    return text;
}
} // namespace rsmb
