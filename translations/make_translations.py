#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Rubic-Labka · Generador de traducciones
---------------------------------------
Genera los .ts (contexto único "Labka") y compila los .qm con lrelease.

Uso:
    python3 make_translations.py
    python3 make_translations.py --ts-only
    python3 make_translations.py --lrelease /usr/lib/qt6/bin/lrelease
    python3 make_translations.py --qm-dir ../build/translations
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
CONTEXT = "Labka"
QM_PREFIX = "rubik-labka_"

# ----------------------------------------------------------------------
# idioma -> { texto_fuente(es): traducción }
# Si falta una clave, Qt muestra el español automáticamente (fallback).
# ----------------------------------------------------------------------
TRANSLATIONS = {
    "en": {
        "Selecciona un Perfil": "Select a Profile",
        "Entrar al Perfil": "Sign In to Profile",
        "+ Crear Nuevo Perfil": "+ Create New Profile",
        "Contraseña incorrecta.": "Incorrect password.",
        "El perfil ya existe.": "The profile already exists.",
        "Las contraseñas no coinciden.": "Passwords do not match.",
        "Subred: %1": "Subnet: %1",
        "Cuenta activa: %1": "Active account: %1",
        "Actualizar / forzar reconexión ahora": "Refresh / force reconnection now",
        "+ Agregar Dispositivo": "+ Add Device",
        "Perfil: %1": "Profile: %1",
        "Auto conexión": "Auto-connect",
        "Idiomas": "Languages",
        "Configuración": "Settings",
        "Cerrar sesión": "Sign out",
        "Salir": "Exit",
        "Transparencia de la ventana:": "Window transparency:",
        "Tamaño de la ventana:": "Window size:",
        "Listo": "Done",
        "Agregar Dispositivo": "Add Device",
        "Dirección IP:": "IP address:",
        "Usuario SSH:": "SSH user:",
        "Contraseña SSH:": "SSH password:",
        "Verificar y Añadir": "Verify and Add",
        "La IP es obligatoria.": "The IP is required.",
        "Verificando conexión SSH...": "Verifying SSH connection...",
        "Conexión fallida": "Connection failed",
        "Abrir Terminal SSH (Bash)": "Open SSH Terminal (Bash)",
        "Explorar Archivos": "Browse Files",
        "Control Remoto (VNC)": "Remote Control (VNC)",
        "Personalizar": "Customize",
        "Quitar/Eliminar": "Remove/Delete",
        "Quitar dispositivo": "Remove device",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "Remove %1 from the map and saved credentials?",
        "Personalizar nodo": "Customize node",
        "Icono para %1:": "Icon for %1:",
        "PC": "PC",
        "Laptop": "Laptop",
        "Placa / Raspberry Pi": "Board / Raspberry Pi",
        "PC 2": "PC 2",
        "No se encontró un emulador de terminal compatible.": "No compatible terminal emulator found.",
        "No se pudo abrir el explorador.": "Could not open the file explorer.",
        "VNC no encontrado": "VNC not found",
        "VNC remoto": "Remote VNC",
        "Error": "Error",
        "SSH: %1": "SSH: %1",
        " · reconectando…": " · reconnecting…",
        " · caído": " · down",
    },
    "fr": {
        "Selecciona un Perfil": "Sélectionnez un profil",
        "Entrar al Perfil": "Se connecter au profil",
        "+ Crear Nuevo Perfil": "+ Créer un nouveau profil",
        "Contraseña incorrecta.": "Mot de passe incorrect.",
        "El perfil ya existe.": "Le profil existe déjà.",
        "Las contraseñas no coinciden.": "Les mots de passe ne correspondent pas.",
        "Subred: %1": "Sous-réseau : %1",
        "Cuenta activa: %1": "Compte actif : %1",
        "Actualizar / forzar reconexión ahora": "Actualiser / forcer la reconnexion",
        "+ Agregar Dispositivo": "+ Ajouter un appareil",
        "Perfil: %1": "Profil : %1",
        "Auto conexión": "Connexion auto",
        "Idiomas": "Langues",
        "Configuración": "Paramètres",
        "Cerrar sesión": "Se déconnecter",
        "Salir": "Quitter",
        "Transparencia de la ventana:": "Transparence de la fenêtre :",
        "Tamaño de la ventana:": "Taille de la fenêtre :",
        "Listo": "OK",
        "Agregar Dispositivo": "Ajouter un appareil",
        "Dirección IP:": "Adresse IP :",
        "Usuario SSH:": "Utilisateur SSH :",
        "Contraseña SSH:": "Mot de passe SSH :",
        "Verificar y Añadir": "Vérifier et ajouter",
        "La IP es obligatoria.": "L'adresse IP est obligatoire.",
        "Verificando conexión SSH...": "Vérification de la connexion SSH...",
        "Conexión fallida": "Échec de la connexion",
        "Abrir Terminal SSH (Bash)": "Ouvrir le terminal SSH (Bash)",
        "Explorar Archivos": "Parcourir les fichiers",
        "Control Remoto (VNC)": "Contrôle à distance (VNC)",
        "Personalizar": "Personnaliser",
        "Quitar/Eliminar": "Supprimer",
        "Quitar dispositivo": "Retirer l'appareil",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "Retirer %1 de la carte et des identifiants enregistrés ?",
        "Personalizar nodo": "Personnaliser le nœud",
        "Icono para %1:": "Icône pour %1 :",
        "PC": "PC",
        "Laptop": "Portable",
        "Placa / Raspberry Pi": "Carte / Raspberry Pi",
        "PC 2": "PC 2",
        "No se encontró un emulador de terminal compatible.": "Aucun émulateur de terminal compatible trouvé.",
        "No se pudo abrir el explorador.": "Impossible d'ouvrir l'explorateur.",
        "VNC no encontrado": "VNC introuvable",
        "VNC remoto": "VNC distant",
        "Error": "Erreur",
        "SSH: %1": "SSH : %1",
        " · reconectando…": " · reconnexion…",
        " · caído": " · hors ligne",
    },
    "de": {
        "Selecciona un Perfil": "Profil auswählen",
        "Entrar al Perfil": "Beim Profil anmelden",
        "+ Crear Nuevo Perfil": "+ Neues Profil erstellen",
        "Contraseña incorrecta.": "Falsches Passwort.",
        "El perfil ya existe.": "Das Profil existiert bereits.",
        "Las contraseñas no coinciden.": "Die Passwörter stimmen nicht überein.",
        "Subred: %1": "Subnetz: %1",
        "Cuenta activa: %1": "Aktives Konto: %1",
        "Actualizar / forzar reconexión ahora": "Aktualisieren / Neuverbindung erzwingen",
        "+ Agregar Dispositivo": "+ Gerät hinzufügen",
        "Perfil: %1": "Profil: %1",
        "Auto conexión": "Auto-Connect",
        "Idiomas": "Sprachen",
        "Configuración": "Einstellungen",
        "Cerrar sesión": "Abmelden",
        "Salir": "Beenden",
        "Transparencia de la ventana:": "Fenstertransparenz:",
        "Tamaño de la ventana:": "Fenstergröße:",
        "Listo": "Fertig",
        "Agregar Dispositivo": "Gerät hinzufügen",
        "Dirección IP:": "IP-Adresse:",
        "Usuario SSH:": "SSH-Benutzer:",
        "Contraseña SSH:": "SSH-Passwort:",
        "Verificar y Añadir": "Prüfen und hinzufügen",
        "La IP es obligatoria.": "Die IP ist erforderlich.",
        "Verificando conexión SSH...": "SSH-Verbindung wird geprüft...",
        "Conexión fallida": "Verbindung fehlgeschlagen",
        "Abrir Terminal SSH (Bash)": "SSH-Terminal öffnen (Bash)",
        "Explorar Archivos": "Dateien durchsuchen",
        "Control Remoto (VNC)": "Fernsteuerung (VNC)",
        "Personalizar": "Anpassen",
        "Quitar/Eliminar": "Entfernen/Löschen",
        "Quitar dispositivo": "Gerät entfernen",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "%1 aus Karte und gespeicherten Zugangsdaten entfernen?",
        "Personalizar nodo": "Knoten anpassen",
        "Icono para %1:": "Symbol für %1:",
        "PC": "PC",
        "Laptop": "Laptop",
        "Placa / Raspberry Pi": "Board / Raspberry Pi",
        "PC 2": "PC 2",
        "No se encontró un emulador de terminal compatible.": "Kein kompatibler Terminal-Emulator gefunden.",
        "No se pudo abrir el explorador.": "Explorer konnte nicht geöffnet werden.",
        "VNC no encontrado": "VNC nicht gefunden",
        "VNC remoto": "VNC (fern)",
        "Error": "Fehler",
        "SSH: %1": "SSH: %1",
        " · reconectando…": " · verbinde neu…",
        " · caído": " · offline",
    },
    "ru": {
        "Selecciona un Perfil": "Выберите профиль",
        "Entrar al Perfil": "Войти в профиль",
        "+ Crear Nuevo Perfil": "+ Создать новый профиль",
        "Contraseña incorrecta.": "Неверный пароль.",
        "El perfil ya existe.": "Профиль уже существует.",
        "Las contraseñas no coinciden.": "Пароли не совпадают.",
        "Subred: %1": "Подсеть: %1",
        "Cuenta activa: %1": "Активная учётная запись: %1",
        "Actualizar / forzar reconexión ahora": "Обновить / принудительно переподключить",
        "+ Agregar Dispositivo": "+ Добавить устройство",
        "Perfil: %1": "Профиль: %1",
        "Auto conexión": "Автоподключение",
        "Idiomas": "Языки",
        "Configuración": "Настройки",
        "Cerrar sesión": "Выйти из сеанса",
        "Salir": "Выход",
        "Transparencia de la ventana:": "Прозрачность окна:",
        "Tamaño de la ventana:": "Размер окна:",
        "Listo": "Готово",
        "Agregar Dispositivo": "Добавить устройство",
        "Dirección IP:": "IP-адрес:",
        "Usuario SSH:": "Пользователь SSH:",
        "Contraseña SSH:": "Пароль SSH:",
        "Verificar y Añadir": "Проверить и добавить",
        "La IP es obligatoria.": "IP обязателен.",
        "Verificando conexión SSH...": "Проверка SSH-соединения...",
        "Conexión fallida": "Ошибка соединения",
        "Abrir Terminal SSH (Bash)": "Открыть SSH-терминал (Bash)",
        "Explorar Archivos": "Обзор файлов",
        "Control Remoto (VNC)": "Удалённое управление (VNC)",
        "Personalizar": "Настроить",
        "Quitar/Eliminar": "Удалить",
        "Quitar dispositivo": "Убрать устройство",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "Убрать %1 с карты и из сохранённых учётных данных?",
        "Personalizar nodo": "Настроить узел",
        "Icono para %1:": "Значок для %1:",
        "PC": "ПК",
        "Laptop": "Ноутбук",
        "Placa / Raspberry Pi": "Плата / Raspberry Pi",
        "PC 2": "ПК 2",
        "No se encontró un emulador de terminal compatible.": "Совместимый эмулятор терминала не найден.",
        "No se pudo abrir el explorador.": "Не удалось открыть проводник.",
        "VNC no encontrado": "VNC не найден",
        "VNC remoto": "Удалённый VNC",
        "Error": "Ошибка",
        "SSH: %1": "SSH: %1",
        " · reconectando…": " · переподключение…",
        " · caído": " · недоступен",
    },
    "zh": {
        "Selecciona un Perfil": "选择配置文件",
        "Entrar al Perfil": "登录配置文件",
        "+ Crear Nuevo Perfil": "+ 新建配置文件",
        "Contraseña incorrecta.": "密码错误。",
        "El perfil ya existe.": "配置文件已存在。",
        "Las contraseñas no coinciden.": "两次密码不一致。",
        "Subred: %1": "子网：%1",
        "Cuenta activa: %1": "当前账户：%1",
        "Actualizar / forzar reconexión ahora": "刷新 / 立即强制重连",
        "+ Agregar Dispositivo": "+ 添加设备",
        "Perfil: %1": "配置文件：%1",
        "Auto conexión": "自动连接",
        "Idiomas": "语言",
        "Configuración": "设置",
        "Cerrar sesión": "退出登录",
        "Salir": "退出",
        "Transparencia de la ventana:": "窗口透明度：",
        "Tamaño de la ventana:": "窗口大小：",
        "Listo": "完成",
        "Agregar Dispositivo": "添加设备",
        "Dirección IP:": "IP 地址：",
        "Usuario SSH:": "SSH 用户：",
        "Contraseña SSH:": "SSH 密码：",
        "Verificar y Añadir": "验证并添加",
        "La IP es obligatoria.": "IP 为必填项。",
        "Verificando conexión SSH...": "正在验证 SSH 连接...",
        "Conexión fallida": "连接失败",
        "Abrir Terminal SSH (Bash)": "打开 SSH 终端 (Bash)",
        "Explorar Archivos": "浏览文件",
        "Control Remoto (VNC)": "远程控制 (VNC)",
        "Personalizar": "自定义",
        "Quitar/Eliminar": "删除",
        "Quitar dispositivo": "移除设备",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "从地图和已保存凭据中移除 %1？",
        "Personalizar nodo": "自定义节点",
        "Icono para %1:": "%1 的图标：",
        "PC": "电脑",
        "Laptop": "笔记本",
        "Placa / Raspberry Pi": "开发板 / Raspberry Pi",
        "PC 2": "电脑 2",
        "No se encontró un emulador de terminal compatible.": "未找到兼容的终端模拟器。",
        "No se pudo abrir el explorador.": "无法打开文件管理器。",
        "VNC no encontrado": "未找到 VNC",
        "VNC remoto": "远程 VNC",
        "Error": "错误",
        "SSH: %1": "SSH：%1",
        " · reconectando…": " · 重连中…",
        " · caído": " · 已掉线",
    },
    "ja": {
        "Selecciona un Perfil": "プロフィールを選択",
        "Entrar al Perfil": "プロフィールにログイン",
        "+ Crear Nuevo Perfil": "+ 新規プロフィール作成",
        "Contraseña incorrecta.": "パスワードが違います。",
        "El perfil ya existe.": "プロフィールは既に存在します。",
        "Las contraseñas no coinciden.": "パスワードが一致しません。",
        "Subred: %1": "サブネット: %1",
        "Cuenta activa: %1": "有効なアカウント: %1",
        "Actualizar / forzar reconexión ahora": "更新 / 今すぐ再接続",
        "+ Agregar Dispositivo": "+ デバイスを追加",
        "Perfil: %1": "プロフィール: %1",
        "Auto conexión": "自動接続",
        "Idiomas": "言語",
        "Configuración": "設定",
        "Cerrar sesión": "ログアウト",
        "Salir": "終了",
        "Transparencia de la ventana:": "ウィンドウの透明度:",
        "Tamaño de la ventana:": "ウィンドウサイズ:",
        "Listo": "完了",
        "Agregar Dispositivo": "デバイスを追加",
        "Dirección IP:": "IP アドレス:",
        "Usuario SSH:": "SSH ユーザー:",
        "Contraseña SSH:": "SSH パスワード:",
        "Verificar y Añadir": "確認して追加",
        "La IP es obligatoria.": "IP は必須です。",
        "Verificando conexión SSH...": "SSH 接続を確認中...",
        "Conexión fallida": "接続に失敗",
        "Abrir Terminal SSH (Bash)": "SSH ターミナルを開く (Bash)",
        "Explorar Archivos": "ファイルを参照",
        "Control Remoto (VNC)": "リモート操作 (VNC)",
        "Personalizar": "カスタマイズ",
        "Quitar/Eliminar": "削除",
        "Quitar dispositivo": "デバイスを削除",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "%1 をマップと保存された認証情報から削除しますか？",
        "Personalizar nodo": "ノードをカスタマイズ",
        "Icono para %1:": "%1 のアイコン:",
        "PC": "PC",
        "Laptop": "ノート PC",
        "Placa / Raspberry Pi": "ボード / Raspberry Pi",
        "PC 2": "PC 2",
        "No se encontró un emulador de terminal compatible.": "互換のあるターミナルが見つかりません。",
        "No se pudo abrir el explorador.": "エクスプローラーを開けませんでした。",
        "VNC no encontrado": "VNC が見つかりません",
        "VNC remoto": "リモート VNC",
        "Error": "エラー",
        "SSH: %1": "SSH: %1",
        " · reconectando…": " · 再接続中…",
        " · caído": " · 切断中",
    },
    "hi": {
        "Selecciona un Perfil": "प्रोफ़ाइल चुनें",
        "Entrar al Perfil": "प्रोफ़ाइल में लॉग इन करें",
        "+ Crear Nuevo Perfil": "+ नई प्रोफ़ाइल बनाएं",
        "Contraseña incorrecta.": "गलत पासवर्ड।",
        "El perfil ya existe.": "प्रोफ़ाइल पहले से मौजूद है।",
        "Las contraseñas no coinciden.": "पासवर्ड मेल नहीं खाते।",
        "Subred: %1": "सबनेट: %1",
        "Cuenta activa: %1": "सक्रिय खाता: %1",
        "Actualizar / forzar reconexión ahora": "रीफ़्रेश / तुरंत पुनः कनेक्ट करें",
        "+ Agregar Dispositivo": "+ डिवाइस जोड़ें",
        "Perfil: %1": "प्रोफ़ाइल: %1",
        "Auto conexión": "ऑटो-कनेक्ट",
        "Idiomas": "भाषाएँ",
        "Configuración": "सेटिंग्स",
        "Cerrar sesión": "लॉग आउट",
        "Salir": "बाहर निकलें",
        "Transparencia de la ventana:": "विंडो पारदर्शिता:",
        "Tamaño de la ventana:": "विंडो आकार:",
        "Listo": "ठीक है",
        "Agregar Dispositivo": "डिवाइस जोड़ें",
        "Dirección IP:": "IP पता:",
        "Usuario SSH:": "SSH उपयोगकर्ता:",
        "Contraseña SSH:": "SSH पासवर्ड:",
        "Verificar y Añadir": "सत्यापित करें और जोड़ें",
        "La IP es obligatoria.": "IP आवश्यक है।",
        "Verificando conexión SSH...": "SSH कनेक्शन सत्यापित हो रहा है...",
        "Conexión fallida": "कनेक्शन विफल",
        "Abrir Terminal SSH (Bash)": "SSH टर्मिनल खोलें (Bash)",
        "Explorar Archivos": "फ़ाइलें ब्राउज़ करें",
        "Control Remoto (VNC)": "दूरस्थ नियंत्रण (VNC)",
        "Personalizar": "अनुकूलित करें",
        "Quitar/Eliminar": "हटाएँ",
        "Quitar dispositivo": "डिवाइस हटाएँ",
        "¿Quitar %1 del mapa y de las credenciales guardadas?": "%1 को मानचित्र और सहेजे गए क्रेडेंशियल से हटाएँ?",
        "Personalizar nodo": "नोड अनुकूलित करें",
        "Icono para %1:": "%1 के लिए आइकन:",
        "PC": "पीसी",
        "Laptop": "लैपटॉप",
        "Placa / Raspberry Pi": "बोर्ड / Raspberry Pi",
        "PC 2": "पीसी 2",
        "No se encontró un emulador de terminal compatible.": "कोई संगत टर्मिनल एम्युलेटर नहीं मिला।",
        "No se pudo abrir el explorador.": "एक्सप्लोरर नहीं खुल सका।",
        "VNC no encontrado": "VNC नहीं मिला",
        "VNC remoto": "दूरस्थ VNC",
        "Error": "त्रुटि",
        "SSH: %1": "SSH: %1",
        " · reconectando…": " · पुनः कनेक्ट…",
        " · caído": " · ऑफ़लाइन",
    },
}


# ----------------------------------------------------------------------
# Descubrimiento de lrelease (Qt6 primero). Se VALIDA ejecutándolo,
# porque /usr/bin/lrelease puede ser el wrapper de qtchooser apuntando
# a un Qt5 inexistente.
# ----------------------------------------------------------------------
def _works(exe):
    if not exe or not os.path.exists(exe):
        return False
    try:
        r = subprocess.run([exe, "-version"],
                           capture_output=True, text=True, timeout=20)
        return r.returncode == 0
    except Exception:
        return False


def _pkgconfig_bins(module):
    try:
        r = subprocess.run(["pkg-config", "--variable=host_bins", module],
                           capture_output=True, text=True, timeout=15)
        if r.returncode == 0:
            p = r.stdout.strip()
            if p:
                return os.path.join(p, "lrelease")
    except Exception:
        pass
    return None


def find_lrelease(explicit=None):
    if explicit:
        if _works(explicit):
            return explicit
        print("[!] El lrelease indicado no funciona: %s" % explicit)
        return None

    tried = []

    env = os.environ.get("LABKA_LRELEASE")
    if env:
        tried.append(env)

    for name in ("lrelease-qt6", "lrelease6", "pyside6-lrelease",
                 "lrelease", "lrelease-qt5"):
        w = shutil.which(name)
        if w:
            tried.append(w)

    for mod in ("Qt6Core", "Qt5Core"):
        p = _pkgconfig_bins(mod)
        if p:
            tried.append(p)

    patterns = [
        "/usr/lib/qt6/bin/lrelease",
        "/usr/lib/*/qt6/bin/lrelease",
        "/usr/lib64/qt6/bin/lrelease",
        "/usr/lib/qt6/libexec/lrelease",
        "/usr/lib/qt5/bin/lrelease",
        "/usr/lib/*/qt5/bin/lrelease",
        "/usr/lib64/qt5/bin/lrelease",
        "/usr/local/opt/qt/bin/lrelease",
        "/opt/homebrew/opt/qt/bin/lrelease",
        os.path.expanduser("~/.local/bin/pyside6-lrelease"),
    ]
    for pat in patterns:
        for p in sorted(glob.glob(pat)):
            tried.append(p)

    seen = set()
    for exe in tried:
        if exe in seen:
            continue
        seen.add(exe)
        if _works(exe):
            return exe

    return None


def write_ts(lang, mapping, path):
    out = []
    out.append('<?xml version="1.0" encoding="utf-8"?>')
    out.append('<!DOCTYPE TS>')
    out.append('<TS version="2.1" language="%s">' % lang)
    out.append('<context>')
    out.append('    <name>%s</name>' % CONTEXT)
    for source, translation in mapping.items():
        out.append('    <message>')
        out.append('        <source>%s</source>' % escape(source))
        out.append('        <translation>%s</translation>' % escape(translation))
        out.append('    </message>')
    out.append('</context>')
    out.append('</TS>')
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


INSTALL_HINT = """
[!] No se encontró un lrelease funcional (Qt6).
    OJO: /usr/bin/lrelease suele ser qtchooser apuntando a Qt5 y falla.

    Debian/Ubuntu : sudo apt install qt6-l10n-tools qt6-tools-dev-tools
    Fedora        : sudo dnf install qt6-linguist qt6-qttools-devel
    Arch          : sudo pacman -S qt6-tools
    openSUSE      : sudo zypper install qt6-linguist-devel
    Alternativa   : pip install pyside6   (usa pyside6-lrelease)

    Luego:  python3 make_translations.py
    O bien: python3 make_translations.py --lrelease /usr/lib/qt6/bin/lrelease
"""


def main():
    ap = argparse.ArgumentParser(description="Genera .ts y .qm de Rubic-Labka")
    ap.add_argument("--lrelease", help="ruta explícita a lrelease")
    ap.add_argument("--ts-only", action="store_true", help="solo generar .ts")
    ap.add_argument("--ts-dir", default=os.path.join(HERE, "ts"))
    ap.add_argument("--qm-dir", default=HERE)
    args = ap.parse_args()

    os.makedirs(args.ts_dir, exist_ok=True)
    os.makedirs(args.qm_dir, exist_ok=True)

    lrelease = None if args.ts_only else find_lrelease(args.lrelease)

    if not args.ts_only:
        if lrelease:
            print("[i] lrelease: %s" % lrelease)
        else:
            print(INSTALL_HINT)

    ok, fail = 0, 0
    for lang, mapping in TRANSLATIONS.items():
        ts_path = os.path.join(args.ts_dir, "%s%s.ts" % (QM_PREFIX, lang))
        write_ts(lang, mapping, ts_path)
        print("[+] .ts: %s (%d strings)" % (ts_path, len(mapping)))

        if not lrelease:
            continue

        qm_path = os.path.join(args.qm_dir, "%s%s.qm" % (QM_PREFIX, lang))
        try:
            subprocess.run([lrelease, ts_path, "-qm", qm_path],
                           check=True, capture_output=True, text=True)
            print("    -> .qm: %s" % qm_path)
            ok += 1
        except subprocess.CalledProcessError as e:
            print("    [x] lrelease falló para %s" % lang)
            if e.stdout:
                print(e.stdout.strip())
            if e.stderr:
                print(e.stderr.strip())
            fail += 1

    print("")
    if args.ts_only:
        print("Listo (solo .ts) en: %s" % args.ts_dir)
        return 0

    if not lrelease:
        print("Los .ts quedaron listos; falta compilar los .qm.")
        return 1

    print("Resumen: %d .qm compilados, %d fallos." % (ok, fail))
    print("Carpeta .qm: %s" % args.qm_dir)
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())