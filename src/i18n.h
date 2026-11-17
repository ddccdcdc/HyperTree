#pragma once
#define NOMINMAX
#include <windows.h>
#include <string>
#include <array>

enum class LangId : int {
    English = 0,
    Arabic,
    Chinese,
    Spanish,
    French,
    German,
    Portuguese,
    Russian,
    Japanese,
    Korean,
    Hindi,
    Turkish,
    Count
};

struct LangStrings {
    const wchar_t* code;
    const wchar_t* native_name;
    bool rtl;

    const wchar_t* app_title;
    const wchar_t* btn_folder;
    const wchar_t* btn_scan;
    const wchar_t* btn_cancel;
    const wchar_t* btn_admin_ready;
    const wchar_t* btn_elevate;
    const wchar_t* btn_export;
    const wchar_t* chk_treemap;
    const wchar_t* chk_system;
    const wchar_t* btn_light;
    const wchar_t* btn_dark;
    const wchar_t* btn_lang;

    const wchar_t* status_ready;
    const wchar_t* tab_tree;
    const wchar_t* tab_files;
    const wchar_t* tab_types;

    const wchar_t* col_name;
    const wchar_t* col_pct_parent;
    const wchar_t* col_size;
    const wchar_t* col_allocated;
    const wchar_t* col_modified;
    const wchar_t* col_files;
    const wchar_t* col_folders;
    const wchar_t* col_name_path;
    const wchar_t* col_ext;
    const wchar_t* col_pct_drive;
    const wchar_t* col_total_size;

    const wchar_t* search_hint;
    const wchar_t* btn_clear;
    const wchar_t* size_all;
    const wchar_t* btn_up;
    const wchar_t* btn_root;
    const wchar_t* treemap_lbl;

    const wchar_t* btn_filters;
    const wchar_t* flt_show_system;
    const wchar_t* flt_show_hidden;
    const wchar_t* flt_show_cloud;
    const wchar_t* flt_hide_small;
    const wchar_t* flt_full_rescan;

    const wchar_t* cat_all;
    const wchar_t* cat_videos;
    const wchar_t* cat_images;
    const wchar_t* cat_audio;
    const wchar_t* cat_archives;
    const wchar_t* cat_docs;
    const wchar_t* cat_exec;
    const wchar_t* cat_code;
    const wchar_t* cat_cloud;

    const wchar_t* age_any;
    const wchar_t* age_24h;
    const wchar_t* age_7d;
    const wchar_t* age_30d;
    const wchar_t* age_1y;

    const wchar_t* lbl_files_unit;
    const wchar_t* lbl_cap_suffix;

    const wchar_t* ctx_open;
    const wchar_t* ctx_explorer;
    const wchar_t* ctx_zoom;
    const wchar_t* ctx_copy;
    const wchar_t* ctx_props;
    const wchar_t* ctx_recycle;
    const wchar_t* ctx_delete;

    const wchar_t* msg_recycle_title;
    const wchar_t* msg_recycle_body;
    const wchar_t* msg_delete_title;
    const wchar_t* msg_delete_body;
    const wchar_t* msg_copied;
    const wchar_t* dlg_lang_title;
    const wchar_t* dlg_lang_prompt;
    const wchar_t* dlg_ok;
};

inline constexpr std::array<LangStrings, static_cast<size_t>(LangId::Count)> g_langs = {{
    // 0: English
    {
        L"en", L"English", false,
        L"HyperTree",
        L"Folder...", L"Scan", L"Cancel", L"Admin (MFT)", L"Elevate", L"Export CSV",
        L"Treemap", L"System Files", L"Light", L"Dark", L"Language",
        L"Select a drive or folder and click Scan.",
        L"Tree View", L"File View", L"File Types",
        L"Name", L"% Parent", L"Size", L"Allocated", L"Modified", L"Files", L"Folders",
        L"Name & Path", L"Extension", L"% of Drive", L"Total Size",
        L"Search name, *.ext, -exclude, >10mb...", L"Clear", L"All sizes", L"Up", L"Root", L"Treemap",
        L"Filters", L"Show System Files", L"Show Hidden Files", L"Show Cloud-Only Files",
        L"Hide Items < 1 MB (Tree & Map)", L"Force Full Rescan (Bypass Cache)",
        L"All Types", L"Videos", L"Images", L"Audio", L"Archives", L"Documents", L"Executables", L"Code", L"Cloud Only",
        L"Any Time", L"Last 24 Hours", L"Last 7 Days", L"Last 30 Days", L"Older than 1 Year",
        L"files", L"[top 200k]",
        L"Open", L"Open in Explorer", L"Zoom Treemap Here", L"Copy Full Path", L"Properties",
        L"Move to Recycle Bin", L"Delete Permanently...",
        L"Confirm Recycle", L"Move this item to the Windows Recycle Bin?\n\n",
        L"Confirm Permanent Delete", L"PERMANENTLY delete this item? This cannot be undone!\n\n",
        L"Copied path to clipboard.",
        L"Select Language", L"Choose your preferred language:", L"Continue"
    },
    // 1: Arabic (RTL)
    {
        L"ar", L"\u0627\u0644\u0639\u0631\u0628\u064A\u0629 (Arabic)", true,
        L"HyperTree - \u0645\u062D\u0644\u0644 \u0645\u0633\u0627\u062D\u0629 \u0627\u0644\u0642\u0631\u0635 \u0641\u0627\u0626\u0642 \u0627\u0644\u0633\u0631\u0639\u0629",
        L"\u0645\u062C\u0644\u062F...", L"\u0641\u062D\u0635", L"\u0625\u0644\u063A\u0627\u0621", L"\u0645\u0633\u0624\u0648\u0644 (MFT)", L"\u062A\u0631\u0642\u064A\u0629", L"\u062A\u0635\u062F\u064A\u0631 CSV",
        L"\u0627\u0644\u062E\u0631\u064A\u0637\u0629", L"\u0645\u0644\u0641\u0627\u062A \u0627\u0644\u0646\u0638\u0627\u0645", L"\u0641\u0627\u062A\u062D", L"\u062F\u0627\u0643\u0646", L"\u0627\u0644\u0644\u063A\u0629",
        L"\u0627\u062E\u062A\u0631 \u0642\u0631\u0635\u0627\u064B \u0623\u0648 \u0645\u062C\u0644\u062F\u0627\u064B \u062B\u0645 \u0627\u0636\u063A\u0637 \u0641\u062D\u0635.",
        L"\u0639\u0631\u0636 \u0627\u0644\u0634\u062C\u0631\u0629", L"\u0639\u0631\u0636 \u0627\u0644\u0645\u0644\u0641\u0627\u062A", L"\u0623\u0646\u0648\u0627\u0639 \u0627\u0644\u0645\u0644\u0641\u0627\u062A",
        L"\u0627\u0644\u0627\u0633\u0645", L"% \u0645\u0646 \u0627\u0644\u0623\u0635\u0644", L"\u0627\u0644\u062D\u062C\u0645", L"\u0627\u0644\u0645\u062E\u0635\u0635", L"\u062A\u0627\u0631\u064A\u062E \u0627\u0644\u062A\u0639\u062F\u064A\u0644", L"\u0627\u0644\u0645\u0644\u0641\u0627\u062A", L"\u0627\u0644\u0645\u062C\u0644\u062F\u0627\u062A",
        L"\u0627\u0644\u0627\u0633\u0645 \u0648\u0627\u0644\u0645\u0633\u0627\u0631", L"\u0627\u0644\u0627\u0645\u062A\u062F\u0627\u062F", L"% \u0645\u0646 \u0627\u0644\u0642\u0631\u0635", L"\u0627\u0644\u062D\u062C\u0645 \u0627\u0644\u0625\u062C\u0645\u0627\u0644\u064A",
        L"\u0627\u0628\u062D\u062B \u0628\u0627\u0644\u0627\u0633\u0645\u060C *.ext\u060C -\u0627\u0633\u062A\u062B\u0646\u0627\u0621\u060C >10mb...", L"\u0645\u0633\u062D", L"\u0643\u0644 \u0627\u0644\u0623\u062D\u062C\u0627\u0645", L"\u0644\u0644\u0623\u0639\u0644\u0649", L"\u0627\u0644\u062C\u0630\u0631", L"\u0627\u0644\u062E\u0631\u064A\u0637\u0629",
        L"\u062A\u0635\u0641\u064A\u0629", L"\u0625\u0638\u0647\u0627\u0631 \u0645\u0644\u0641\u0627\u062A \u0627\u0644\u0646\u0638\u0627\u0645", L"\u0625\u0638\u0647\u0627\u0631 \u0627\u0644\u0645\u0644\u0641\u0627\u062A \u0627\u0644\u0645\u062E\u0641\u064A\u0629", L"\u0625\u0638\u0647\u0627\u0631 \u0645\u0644\u0641\u0627\u062A \u0627\u0644\u0633\u062D\u0627\u0628\u0629 \u0641\u0642\u0637",
        L"\u0625\u062E\u0641\u0627\u0621 < 1 \u0645.\u0628 (\u0627\u0644\u0634\u062C\u0631\u0629 \u0648\u0627\u0644\u062E\u0631\u064A\u0637\u0629)", L"\u0625\u0639\u0627\u062F\u0629 \u0641\u062D\u0635 \u0643\u0627\u0645\u0644 (\u062A\u062C\u0627\u0648\u0632 \u0627\u0644\u0630\u0627\u0643\u0631\u0629 \u0627\u0644\u0645\u0624\u0642\u062A\u0629)",
        L"\u0643\u0644 \u0627\u0644\u0623\u0646\u0648\u0627\u0639", L"\u0641\u064A\u062F\u064A\u0648", L"\u0635\u0648\u0631", L"\u0635\u0648\u062A\u064A\u0627\u062A", L"\u0623\u0631\u0634\u064A\u0641", L"\u0645\u0633\u062A\u0646\u062F\u0627\u062A", L"\u062A\u0646\u0641\u064A\u0630\u064A\u0629", L"\u0623\u0643\u0648\u0627\u062F", L"\u0633\u062D\u0627\u0628\u064A \u0641\u0642\u0637",
        L"\u0623\u064A \u0648\u0642\u062A", L"\u0622\u062E\u0631 24 \u0633\u0627\u0639\u0629", L"\u0622\u062E\u0631 7 \u0623\u064A\u0627\u0645", L"\u0622\u062E\u0631 30 \u064A\u0648\u0645\u0627\u064B", L"\u0623\u0642\u062F\u0645 \u0645\u0646 \u0633\u0646\u0629",
        L"\u0645\u0644\u0641", L"[\u0623\u0643\u0628\u0631 200 \u0623\u0644\u0641]",
        L"\u0641\u062A\u062D", L"\u0641\u062A\u062D \u0641\u064A \u0627\u0644\u0645\u0633\u062A\u0643\u0634\u0641", L"\u062A\u0643\u0628\u064A\u0631 \u0627\u0644\u062E\u0631\u064A\u0637\u0629 \u0647\u0646\u0627", L"\u0646\u0633\u062E \u0627\u0644\u0645\u0633\u0627\u0631 \u0627\u0644\u0643\u0627\u0645\u0644", L"\u062E\u0635\u0627\u0626\u0635",
        L"\u0646\u0642\u0644 \u0625\u0644\u0649 \u0633\u0644\u0629 \u0627\u0644\u0645\u062D\u0630\u0648\u0641\u0627\u062A", L"\u062D\u0630\u0641 \u0646\u0647\u0627\u0626\u064A...",
        L"\u062A\u0623\u0643\u064A\u062F \u0627\u0644\u0646\u0642\u0644", L"\u0647\u0644 \u062A\u0631\u064A\u062F \u0646\u0642\u0644 \u0647\u0630\u0627 \u0627\u0644\u0639\u0646\u0635\u0631 \u0625\u0644\u0649 \u0633\u0644\u0629 \u0627\u0644\u0645\u062D\u0630\u0648\u0641\u0627\u062A\u061F\n\n",
        L"\u062A\u0623\u0643\u064A\u062F \u0627\u0644\u062D\u0630\u0641 \u0627\u0644\u0646\u0647\u0627\u0626\u064A", L"\u0647\u0644 \u062A\u0631\u064A\u062F \u062D\u0630\u0641 \u0647\u0630\u0627 \u0627\u0644\u0639\u0646\u0635\u0631 \u0646\u0647\u0627\u0626\u064A\u0627\u064B\u061F \u0644\u0627 \u064A\u0645\u0643\u0646 \u0627\u0644\u062A\u0631\u0627\u062C\u0639!\n\n",
        L"\u062A\u0645 \u0646\u0633\u062E \u0627\u0644\u0645\u0633\u0627\u0631 \u0625\u0644\u0649 \u0627\u0644\u062D\u0627\u0641\u0638\u0629.",
        L"\u0627\u062E\u062A\u0631 \u0627\u0644\u0644\u063A\u0629", L"\u0627\u062E\u062A\u0631 \u0644\u063A\u062A\u0643 \u0627\u0644\u0645\u0641\u0636\u0644\u0629:", L"\u0645\u062A\u0627\u0628\u0639\u0629"
    },
    // 2: Chinese Simplified
    {
        L"zh", L"\u4E2D\u6587 (\u7B80\u4F53)", false,
        L"HyperTree - \u6781\u901F\u78C1\u76D8\u7A7A\u95F4\u5206\u6790\u5668",
        L"\u6587\u4EF6\u5939...", L"\u626B\u63CF", L"\u53D6\u6D88", L"\u7BA1\u7406\u5458 (MFT)", L"\u63D0\u6743", L"\u5BFC\u51FA CSV",
        L"\u6811\u72B6\u56FE", L"\u7CFB\u7EDF\u6587\u4EF6", L"\u6D45\u8272", L"\u6DF1\u8272", L"\u8BED\u8A00",
        L"\u8BF7\u9009\u62E9\u9A71\u52A8\u5668\u6216\u6587\u4EF6\u5939\u5E76\u70B9\u51FB\u626B\u63CF\u3002",
        L"\u6811\u5F62\u89C6\u56FE", L"\u6587\u4EF6\u89C6\u56FE", L"\u6587\u4EF6\u7C7B\u578B",
        L"\u540D\u79F0", L"\u5360\u7236\u7EA7%", L"\u5927\u5C0F", L"\u5DF2\u5206\u914D", L"\u4FEE\u6539\u65F6\u95F4", L"\u6587\u4EF6\u6570", L"\u6587\u4EF6\u5939\u6570",
        L"\u540D\u79F0\u4E0E\u8DEF\u5F84", L"\u6269\u5C55\u540D", L"\u5360\u78C1\u76D8%", L"\u603B\u5927\u5C0F",
        L"\u641C\u7D22\u6587\u4EF6\u540D\u3001*.ext\u3001-\u6392\u9664\u3001>10mb...", L"\u6E05\u9664", L"\u6240\u6709\u5927\u5C0F", L"\u4E0A\u4E00\u7EA7", L"\u6839\u76EE\u5F55", L"\u6811\u72B6\u56FE",
        L"\u7B5B\u9009", L"\u663E\u793A\u7CFB\u7EDF\u6587\u4EF6", L"\u663E\u793A\u9690\u85CF\u6587\u4EF6", L"\u663E\u793A\u4EC5\u4E91\u7AEF\u6587\u4EF6",
        L"\u9690\u85CF < 1 MB \u9879 (\u6811\u4E0E\u56FE)", L"\u5F3A\u5236\u5B8C\u6574\u91CD\u626B (\u8DF3\u8FC7\u7F13\u5B58)",
        L"\u6240\u6709\u7C7B\u578B", L"\u89C6\u9891", L"\u56FE\u7247", L"\u97F3\u9891", L"\u538B\u7F29\u5305", L"\u6587\u6863", L"\u53EF\u6267\u884C\u6587\u4EF6", L"\u4EE3\u7801", L"\u4EC5\u4E91\u7AEF",
        L"\u4EFB\u610F\u65F6\u95F4", L"\u8FC7\u53BB 24 \u5C0F\u65F6", L"\u8FC7\u53BB 7 \u5929", L"\u8FC7\u53BB 30 \u5929", L"1 \u5E74\u4EE5\u524D",
        L"\u4E2A\u6587\u4EF6", L"[\u524D 20\u4E07]",
        L"\u6253\u5F00", L"\u5728\u8D44\u6E90\u7BA1\u7406\u5668\u4E2D\u6253\u5F00", L"\u5728\u6B64\u5904\u7F29\u653E\u6811\u72B6\u56FE", L"\u590D\u5236\u5B8C\u6574\u8DEF\u5F84", L"\u5C5E\u6027",
        L"\u79FB\u81F3\u56DE\u6536\u7AD9", L"\u6C38\u4E45\u5220\u9664...",
        L"\u786E\u8BA4\u5220\u9664", L"\u786E\u5B9A\u8981\u5C06\u6B64\u9879\u76EE\u79FB\u81F3\u56DE\u6536\u7AD9\u5417\uFF1F\n\n",
        L"\u786E\u8BA4\u6C38\u4E45\u5220\u9664", L"\u786E\u5B9A\u8981\u6C38\u4E45\u5220\u9664\u6B64\u9879\u76EE\u5417\uFF1F\u6B64\u64CD\u4F5C\u65E0\u6CD5\u64A4\u9500\uFF01\n\n",
        L"\u5DF2\u590D\u5236\u8DEF\u5F84\u5230\u526A\u8D34\u677F\u3002",
        L"\u9009\u62E9\u8BED\u8A00", L"\u8BF7\u9009\u62E9\u60A8\u504F\u597D\u7684\u8BED\u8A00\uFF1A", L"\u7EE7\u7EED"
    },
    // 3: Spanish
    {
        L"es", L"Espa\u00F1ol", false,
        L"HyperTree - Analizador de Espacio en Disco",
        L"Carpeta...", L"Escanear", L"Cancelar", L"Admin (MFT)", L"Elevar", L"Exportar CSV",
        L"Mapa", L"Archivos del sistema", L"Claro", L"Oscuro", L"Idioma",
        L"Seleccione una unidad o carpeta y haga clic en Escanear.",
        L"Vista de \u00C1rbol", L"Vista de Archivos", L"Tipos de Archivo",
        L"Nombre", L"% Padre", L"Tama\u00F1o", L"Asignado", L"Modificado", L"Archivos", L"Carpetas",
        L"Nombre y Ruta", L"Extensi\u00F3n", L"% Unidad", L"Tama\u00F1o Total",
        L"Buscar nombre, *.ext, -excluir, >10mb...", L"Limpiar", L"Todos", L"Subir", L"Ra\u00EDz", L"Mapa",
        L"Filtros", L"Mostrar archivos del sistema", L"Mostrar archivos ocultos", L"Mostrar archivos en la nube",
        L"Ocultar < 1 MB (\u00C1rbol y Mapa)", L"Forzar escaneo completo (sin cach\u00E9)",
        L"Todos los tipos", L"Videos", L"Im\u00E1genes", L"Audio", L"Archivos", L"Documentos", L"Ejecutables", L"C\u00F3digo", L"Solo en la nube",
        L"Cualquier fecha", L"\u00DAltimas 24 h", L"\u00DAltimos 7 d\u00EDas", L"\u00DAltimos 30 d\u00EDas", L"M\u00E1s de 1 a\u00F1o",
        L"archivos", L"[top 200k]",
        L"Abrir", L"Abrir en el Explorador", L"Acercar Mapa Aqu\u00ED", L"Copiar Ruta Completa", L"Propiedades",
        L"Mover a la Papelera", L"Eliminar Permanentemente...",
        L"Confirmar Papelera", L"\u00BFMover este elemento a la Papelera de reciclaje?\n\n",
        L"Confirmar Eliminaci\u00F3n", L"\u00BFEliminar PERMANENTEMENTE este elemento?\n\n",
        L"Ruta copiada al portapapeles.",
        L"Seleccionar Idioma", L"Elija su idioma preferido:", L"Continuar"
    },
    // 4: French
    {
        L"fr", L"Fran\u00E7ais", false,
        L"HyperTree - Analyseur d'Espace Disque",
        L"Dossier...", L"Analyser", L"Annuler", L"Admin (MFT)", L"\u00C9lever", L"Exporter CSV",
        L"Carte", L"Fichiers syst\u00E8me", L"Clair", L"Sombre", L"Langue",
        L"S\u00E9lectionnez un lecteur ou un dossier et cliquez sur Analyser.",
        L"Arborescence", L"Fichiers", L"Types de Fichiers",
        L"Nom", L"% Parent", L"Taille", L"Allou\u00E9", L"Modifi\u00E9", L"Fichiers", L"Dossiers",
        L"Nom et Chemin", L"Extension", L"% Disque", L"Taille Totale",
        L"Nom, *.ext, -exclure, >10mb...", L"Effacer", L"Toutes tailles", L"Haut", L"Racine", L"Carte",
        L"Filtres", L"Afficher fichiers syst\u00E8me", L"Afficher fichiers cach\u00E9s", L"Afficher fichiers Cloud uniquement",
        L"Masquer < 1 Mo (Arbre & Carte)", L"Forcer analyse compl\u00E8te (sans cache)",
        L"Tous les types", L"Vid\u00E9os", L"Images", L"Audio", L"Archives", L"Documents", L"Ex\u00E9cutables", L"Code", L"Cloud uniquement",
        L"Toute date", L"Dernières 24 h", L"7 derniers jours", L"30 derniers jours", L"Plus d'un an",
        L"fichiers", L"[top 200k]",
        L"Ouvrir", L"Ouvrir dans l'Explorateur", L"Zoomer ici", L"Copier le chemin", L"Propri\u00E9t\u00E9s",
        L"Mettre \u00E0 la Corbeille", L"Supprimer d\u00E9finitivement...",
        L"Confirmer Corbeille", L"D\u00E9placer cet \u00E9l\u00E9ment vers la Corbeille ?\n\n",
        L"Suppression d\u00E9finitive", L"Supprimer D\u00C9FINITIVEMENT cet \u00E9l\u00E9ment ?\n\n",
        L"Chemin copi\u00E9.",
        L"Choisir la langue", L"S\u00E9lectionnez votre langue :", L"Continuer"
    },
    // 5: German
    {
        L"de", L"Deutsch", false,
        L"HyperTree - Speicherplatz-Analysator",
        L"Ordner...", L"Scannen", L"Abbrechen", L"Admin (MFT)", L"Rechte erh\u00F6hen", L"CSV Export",
        L"Treemap", L"Systemdateien", L"Hell", L"Dunkel", L"Sprache",
        L"W\u00E4hlen Sie ein Laufwerk oder einen Ordner und klicken Sie auf Scannen.",
        L"Baumansicht", L"Dateiansicht", L"Dateitypen",
        L"Name", L"% Eltern", L"Gr\u00F6\u00DFe", L"Belegt", L"Ge\u00E4ndert", L"Dateien", L"Ordner",
        L"Name & Pfad", L"Endung", L"% Laufwerk", L"Gesamtgr\u00F6\u00DFe",
        L"Name, *.ext, -Ausschlu\u00DF, >10mb...", L"Leeren", L"Alle Gr\u00F6\u00DFen", L"Hoch", L"Stamm", L"Treemap",
        L"Filter", L"Systemdateien anzeigen", L"Versteckte Dateien anzeigen", L"Cloud-Dateien anzeigen",
        L"< 1 MB ausblenden (Baum & Map)", L"Vollscan erzwingen (ohne Cache)",
        L"Alle Typen", L"Videos", L"Bilder", L"Audio", L"Archive", L"Dokumente", L"Ausf\u00FChrbar", L"Code", L"Nur Cloud",
        L"Jederzeit", L"Letzte 24 Std.", L"Letzte 7 Tage", L"Letzte 30 Tage", L"\u00C4lter als 1 Jahr",
        L"Dateien", L"[Top 200k]",
        L"\u00D6ffnen", L"Im Explorer \u00F6ffnen", L"Treemap hierher zoomen", L"Pfad kopieren", L"Eigenschaften",
        L"In Papierkorb verschieben", L"Endg\u00FCltig l\u00F6schen...",
        L"Papierkorb best\u00E4tigen", L"Dieses Element in den Papierkorb verschieben?\n\n",
        L"Endg\u00FCltig l\u00F6schen", L"Dieses Element ENDG\u00DCLTIG l\u00F6schen?\n\n",
        L"Pfad in Zwischenablage kopiert.",
        L"Sprache w\u00E4hlen", L"Bitte w\u00E4hlen Sie Ihre Sprache:", L"Weiter"
    },
    // 6: Portuguese
    {
        L"pt", L"Portugu\u00EAs", false,
        L"HyperTree - Analisador de Espa\u00E7o em Disco",
        L"Pasta...", L"Verificar", L"Cancelar", L"Admin (MFT)", L"Elevar", L"Exportar CSV",
        L"Mapa", L"Arquivos de sistema", L"Claro", L"Escuro", L"Idioma",
        L"Selecione uma unidade ou pasta e clique em Verificar.",
        L"\u00C1rvore", L"Arquivos", L"Tipos de Arquivo",
        L"Nome", L"% Pai", L"Tamanho", L"Alocado", L"Modificado", L"Arquivos", L"Pastas",
        L"Nome e Caminho", L"Extens\u00E3o", L"% Disco", L"Tamanho Total",
        L"Nome, *.ext, -excluir, >10mb...", L"Limpar", L"Todos", L"Acima", L"Raiz", L"Mapa",
        L"Filtros", L"Mostrar arquivos de sistema", L"Mostrar arquivos ocultos", L"Mostrar arquivos na nuvem",
        L"Ocultar < 1 MB (\u00C1rvore e Mapa)", L"For\u00E7ar verifica\u00E7\u00E3o completa",
        L"Todos os tipos", L"V\u00EDdeos", L"Imagens", L"\u00C1udio", L"Arquivos", L"Documentos", L"Execut\u00E1veis", L"C\u00F3digo", L"Apenas na nuvem",
        L"Qualquer data", L"\u00DAltimas 24 h", L"\u00DAltimos 7 dias", L"\u00DAltimos 30 dias", L"Mais de 1 ano",
        L"arquivos", L"[top 200k]",
        L"Abrir", L"Abrir no Explorer", L"Ampliar Mapa Aqui", L"Copiar Caminho", L"Propriedades",
        L"Mover para a Lixeira", L"Excluir Permanentemente...",
        L"Confirmar Lixeira", L"Mover este item para a Lixeira?\n\n",
        L"Exclus\u00E3o Permanente", L"Excluir PERMANENTEMENTE este item?\n\n",
        L"Caminho copiado.",
        L"Selecionar Idioma", L"Escolha seu idioma preferido:", L"Continuar"
    },
    // 7: Russian
    {
        L"ru", L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439", false,
        L"HyperTree - \u0410\u043D\u0430\u043B\u0438\u0437\u0430\u0442\u043E\u0440 \u0434\u0438\u0441\u043A\u043E\u0432\u043E\u0433\u043E \u043F\u0440\u043E\u0441\u0442\u0440\u0430\u043D\u0441\u0442\u0432\u0430",
        L"\u041F\u0430\u043F\u043A\u0430...", L"\u0421\u043A\u0430\u043D", L"\u041E\u0442\u043C\u0435\u043D\u0430", L"\u0410\u0434\u043C\u0438\u043D (MFT)", L"\u041F\u043E\u0432\u044B\u0441\u0438\u0442\u044C", L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442 CSV",
        L"\u041A\u0430\u0440\u0442\u0430", L"\u0421\u0438\u0441\u0442\u0435\u043C\u043D\u044B\u0435", L"\u0421\u0432\u0435\u0442\u043B\u0430\u044F", L"\u0422\u0435\u043C\u043D\u0430\u044F", L"\u042F\u0437\u044B\u043A",
        L"\u0412\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0438\u0441\u043A \u0438\u043B\u0438 \u043F\u0430\u043F\u043A\u0443 \u0438 \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u0421\u043A\u0430\u043D.",
        L"\u0414\u0435\u0440\u0435\u0432\u043E", L"\u0424\u0430\u0439\u043B\u044B", L"\u0422\u0438\u043F\u044B \u0444\u0430\u0439\u043B\u043E\u0432",
        L"\u0418\u043C\u044F", L"% \u0420\u043E\u0434.", L"\u0420\u0430\u0437\u043C\u0435\u0440", L"\u0417\u0430\u043D\u044F\u0442\u043E", L"\u0418\u0437\u043C\u0435\u043D\u0435\u043D", L"\u0424\u0430\u0439\u043B\u044B", L"\u041F\u0430\u043F\u043A\u0438",
        L"\u0418\u043C\u044F \u0438 \u043F\u0443\u0442\u044C", L"\u0420\u0430\u0441\u0448\u0438\u0440\u0435\u043D\u0438\u0435", L"% \u0414\u0438\u0441\u043A\u0430", L"\u041E\u0431\u0449\u0438\u0439 \u0440\u0430\u0437\u043C\u0435\u0440",
        L"\u0418\u043C\u044F, *.ext, -\u0438\u0441\u043A\u043B\u044E\u0447\u0438\u0442\u044C, >10mb...", L"\u0421\u0431\u0440\u043E\u0441", L"\u0412\u0441\u0435", L"\u0412\u0432\u0435\u0440\u0445", L"\u041A\u043E\u0440\u0435\u043D\u044C", L"\u041A\u0430\u0440\u0442\u0430",
        L"\u0424\u0438\u043B\u044C\u0442\u0440\u044B", L"\u041F\u043E\u043A\u0430\u0437\u044B\u0432\u0430\u0442\u044C \u0441\u0438\u0441\u0442\u0435\u043C\u043D\u044B\u0435", L"\u041F\u043E\u043A\u0430\u0437\u044B\u0432\u0430\u0442\u044C \u0441\u043A\u0440\u044B\u0442\u044B\u0435", L"\u041F\u043E\u043A\u0430\u0437\u044B\u0432\u0430\u0442\u044C \u043E\u0431\u043B\u0430\u0447\u043D\u044B\u0435",
        L"\u0421\u043A\u0440\u044B\u0442\u044C < 1 \u041C\u0411 (\u0414\u0435\u0440\u0435\u0432\u043E/\u041A\u0430\u0440\u0442\u0430)", L"\u041F\u043E\u043B\u043D\u043E\u0435 \u0441\u043A\u0430\u043D\u0438\u0440\u043E\u0432\u0430\u043D\u0438\u0435 (\u0431\u0435\u0437 \u043A\u044D\u0448\u0430)",
        L"\u0412\u0441\u0435 \u0442\u0438\u043F\u044B", L"\u0412\u0438\u0434\u0435\u043E", L"\u0418\u0437\u043E\u0431\u0440\u0430\u0436\u0435\u043D\u0438\u044F", L"\u0410\u0443\u0434\u0438\u043E", L"\u0410\u0440\u0445\u0438\u0432\u044B", L"\u0414\u043E\u043A\u0443\u043C\u0435\u043D\u0442\u044B", L"\u041F\u0440\u043E\u0433\u0440\u0430\u043C\u043C\u044B", L"\u041A\u043E\u0434", L"\u0422\u043E\u043B\u044C\u043A\u043E \u043E\u0431\u043B\u0430\u043A\u043E",
        L"\u041B\u044E\u0431\u043E\u0435 \u0432\u0440\u0435\u043C\u044F", L"\u0417\u0430 24 \u0447\u0430\u0441\u0430", L"\u0417\u0430 7 \u0434\u043D\u0435\u0439", L"\u0417\u0430 30 \u0434\u043D\u0435\u0439", L"\u0421\u0442\u0430\u0440\u0448\u0435 1 \u0433\u043E\u0434\u0430",
        L"\u0444\u0430\u0439\u043B\u043E\u0432", L"[\u0442\u043E\u043F 200\u043A]",
        L"\u041E\u0442\u043A\u0440\u044B\u0442\u044C", L"\u041F\u043E\u043A\u0430\u0437\u0430\u0442\u044C \u0432 \u041F\u0440\u043E\u0432\u043E\u0434\u043D\u0438\u043A\u0435", L"\u041F\u0440\u0438\u0431\u043B\u0438\u0437\u0438\u0442\u044C \u043A\u0430\u0440\u0442\u0443", L"\u041A\u043E\u043F\u0438\u0440\u043E\u0432\u0430\u0442\u044C \u043F\u0443\u0442\u044C", L"\u0421\u0432\u043E\u0439\u0441\u0442\u0432\u0430",
        L"\u0412 \u041A\u043E\u0440\u0437\u0438\u043D\u0443", L"\u0423\u0434\u0430\u043B\u0438\u0442\u044C \u043D\u0430\u0432\u0441\u0435\u0433\u0434\u0430...",
        L"\u041A\u043E\u0440\u0437\u0438\u043D\u0430", L"\u041F\u0435\u0440\u0435\u043C\u0435\u0441\u0442\u0438\u0442\u044C \u0432 \u041A\u043E\u0440\u0437\u0438\u043D\u0443?\n\n",
        L"\u0423\u0434\u0430\u043B\u0435\u043D\u0438\u0435", L"\u0423\u0434\u0430\u043B\u0438\u0442\u044C \u041D\u0410\u0412\u0421\u0415\u0413\u0414\u0410?\n\n",
        L"\u041F\u0443\u0442\u044C \u0441\u043A\u043E\u043F\u0438\u0440\u043E\u0432\u0430\u043D.",
        L"\u0412\u044B\u0431\u043E\u0440 \u044F\u0437\u044B\u043A\u0430", L"\u0412\u044B\u0431\u0435\u0440\u0438\u0442\u0435 \u044F\u0437\u044B\u043A \u0438\u043D\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430:", L"\u041F\u0440\u043E\u0434\u043E\u043B\u0436\u0438\u0442\u044C"
    },
    // 8: Japanese
    {
        L"ja", L"\u65E5\u672C\u8A9E", false,
        L"HyperTree - \u8D85\u9AD8\u901F\u30C7\u30A3\u30B9\u30AF\u5BB9\u91CF\u5206\u6790",
        L"\u30D5\u30A9\u30EB\u30C0...", L"\u30B9\u30AD\u30E3\u30F3", L"\u4E2D\u6B62", L"\u7BA1\u7406\u8005 (MFT)", L"\u6A29\u9650\u6607\u683C", L"CSV\u51FA\u529B",
        L"\u30C4\u30EA\u30FC\u30DE\u30C3\u30D7", L"\u30B7\u30B9\u30C6\u30E0", L"\u30E9\u30A4\u30C8", L"\u30C0\u30FC\u30AF", L"\u8A00\u8A9E",
        L"\u30C9\u30E9\u30A4\u30D6\u307E\u305F\u306F\u30D5\u30A9\u30EB\u30C0\u3092\u9078\u629E\u3057\u3066\u30B9\u30AD\u30E3\u30F3\u3092\u62BC\u3057\u3066\u304F\u3060\u3055\u3044\u3002",
        L"\u30C4\u30EA\u30FC\u8868\u793A", L"\u30D5\u30A1\u30A4\u30EB\u4E00\u89A7", L"\u62E1\u5F35\u5B50\u5225",
        L"\u540D\u524D", L"\u89AA\u6BD4\u7387", L"\u30B5\u30A4\u30BA", L"\u5272\u5F53\u6E08", L"\u66F4\u65B0\u65E5\u6642", L"\u30D5\u30A1\u30A4\u30EB", L"\u30D5\u30A9\u30EB\u30C0",
        L"\u30D5\u30EB\u30D1\u30B9", L"\u62E1\u5F35\u5B50", L"\u30C9\u30E9\u30A4\u30D6%", L"\u5408\u8A08\u30B5\u30A4\u30BA",
        L"\u540D\u524D\u3001*.ext\u3001-\u9664\u5916\u3001>10mb...", L"\u30AF\u30EA\u30A2", L"\u3059\u3079\u3066", L"\u4E0A\u3078", L"\u30EB\u30FC\u30C8", L"\u30DE\u30C3\u30D7",
        L"\u30D5\u30A3\u30EB\u30BF", L"\u30B7\u30B9\u30C6\u30E0\u30D5\u30A1\u30A4\u30EB\u3092\u8868\u793A", L"\u96A0\u3057\u30D5\u30A1\u30A4\u30EB\u3092\u8868\u793A", L"\u30AF\u30E9\u30A6\u30C9\u5C02\u7528\u3092\u8868\u793A",
        L"1 MB \u672A\u6E80\u3092\u975E\u8868\u793A (\u30C4\u30EA\u30FC/\u30DE\u30C3\u30D7)", L"\u5B8C\u5168\u518D\u30B9\u30AD\u30E3\u30F3 (\u30AD\u30E3\u30C3\u30B7\u30E5\u7121\u8996)",
        L"\u3059\u3079\u3066\u306E\u7A2E\u985E", L"\u52D5\u753B", L"\u753B\u50CF", L"\u97F3\u58F0", L"\u5727\u7E2E", L"\u6587\u66F8", L"\u5B9F\u884C\u30D5\u30A1\u30A4\u30EB", L"\u30B3\u30FC\u30C9", L"\u30AF\u30E9\u30A6\u30C9\u306E\u307F",
        L"\u3059\u3079\u3066\u306E\u671F\u9593", L"24\u6642\u9593\u4EE5\u5185", L"7\u65E5\u4EE5\u5185", L"30\u65E5\u4EE5\u5185", L"1\u5E74\u4EE5\u4E0A\u524D",
        L"\u4EF6", L"[\u4E0A\u4F4D 20\u4E07]",
        L"\u958B\u304F", L"\u30A8\u30AF\u30B9\u30D7\u30ED\u30FC\u30E9\u30FC\u3067\u958B\u304F", L"\u3053\u3053\u3092\u62E1\u5927", L"\u30D1\u30B9\u3092\u30B3\u30D4\u30FC", L"\u30D7\u30ED\u30D1\u30C6\u30A3",
        L"\u3054\u307F\u7BB1\u3078\u79FB\u52D5", L"\u5B8C\u5168\u306B\u524A\u9664...",
        L"\u524A\u9664\u306E\u78BA\u8A8D", L"\u3054\u307F\u7BB1\u3078\u79FB\u52D5\u3057\u307E\u3059\u304B\uFF1F\n\n",
        L"\u5B8C\u5168\u524A\u9664", L"\u5B8C\u5168\u306B\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F\u5143\u306B\u623B\u305B\u307E\u305B\u3093\u3002\n\n",
        L"\u30D1\u30B9\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F\u3002",
        L"\u8A00\u8A9E\u306E\u9078\u629E", L"\u4F7F\u7528\u3059\u308B\u8A00\u8A9E\u3092\u9078\u629E\u3057\u3066\u304F\u3060\u3055\u3044\uFF1A", L"\u7D9A\u884C"
    },
    // 9: Korean
    {
        L"ko", L"\uD55C\uAD6D\uC5B4", false,
        L"HyperTree - \uCD08\uACE0\uC18D \uB514\uC2A4\uD06C \uACF5\uAC04 \uBD84\uC11D\uAE30",
        L"\uD3F4\uB354...", L"\uC2A4\uCE94", L"\uCDE8\uC18C", L"\uAD00\uB9AC\uC790 (MFT)", L"\uAD8C\uD55C \uC0C1\uC2B9", L"CSV \uB0B4\uBCF4\uB0B4\uAE30",
        L"\uD2B8\uB9AC\uB9F5", L"\uC2DC\uC2A4\uD15C \uD30C\uC77C", L"\uB77C\uC774\uD2B8", L"\uB2E4\uD06C", L"\uC5B8\uC5B4",
        L"\uB4DC\uB77C\uC774\uBE0C \uB610\uB294 \uD3F4\uB354\uB97C \uC120\uD0DD\uD558\uACE0 \uC2A4\uCE94\uC744 \uD074\uB9AD\uD558\uC138\uC694.",
        L"\uD2B8\uB9AC \uBCF4\uAE30", L"\uD30C\uC77C \uBCF4\uAE30", L"\uD30C\uC77C \uD615\uC2DD",
        L"\uC774\uB984", L"% \uC0C1\uC704", L"\uD06C\uAE30", L"\uD560\uB2F9\uB428", L"\uC218\uC815\uB428", L"\uD30C\uC77C", L"\uD3F4\uB354",
        L"\uC774\uB984 \uBC0F \uACBD\uB85C", L"\uD655\uC7A5\uC790", L"% \uB4DC\uB77C\uC774\uBE0C", L"\uCD1D \uD06C\uAE30",
        L"\uC774\uB984, *.ext, -\uC81C\uC678, >10mb...", L"\uC9C0\uC6B0\uAE30", L"\uBAA8\uB4E0 \uD06C\uAE30", L"\uC704\uB85C", L"\uB8E8\uD2B8", L"\uD2B8\uB9AC\uB9F5",
        L"\uD544\uD130", L"\uC2DC\uC2A4\uD15C \uD30C\uC77C \uD45C\uC2DC", L"\uC228\uAE40 \uD30C\uC77C \uD45C\uC2DC", L"\uD074\uB77C\uC6B0\uB4DC \uC804\uC6A9 \uD45C\uC2DC",
        L"1 MB \uBBF8\uB9CC \uC228\uAE30\uAE30 (\uD2B8\uB9AC/\uB9F5)", L"\uC804\uCCB4 \uB2E4\uC2DC \uC2A4\uCE94 (\uCE90\uC2DC \uBB34\uC2DC)",
        L"\uBAA8\uB4E0 \uC720\uD615", L"\uB3D9\uC601\uC0C1", L"\uC774\uBBF8\uC9C0", L"\uC624\uB514\uC624", L"\uC555\uCD95 \uD30C\uC77C", L"\uBB38\uC11C", L"\uC2E4\uD589 \uD30C\uC77C", L"\uCF54\uB4DC", L"\uD074\uB77C\uC6B0\uB4DC \uC804\uC6A9",
        L"\uC804\uCCB4 \uAE30\uAC04", L"\uCD5C\uADFC 24\uC2DC\uAC04", L"\uCD5C\uADFC 7\uC77C", L"\uCD5C\uADFC 30\uC77C", L"1\uB144 \uC774\uC0C1",
        L"\uAC1C \uD30C\uC77C", L"[\uC0C1\uC704 20\uB9CC]",
        L"\uC5F4\uAE30", L"\uD0D0\uC0C9\uAE30\uC5D0\uC11C \uC5F4\uAE30", L"\uC5EC\uAE30 \uD655\uB300", L"\uC804\uCCB4 \uACBD\uB85C \uBCF5\uC0AC", L"\uC18D\uC131",
        L"\uD734\uC9C0\uD1B5\uC73C\uB85C \uC774\uB3D9", L"\uC601\uAD6C \uC0AD\uC81C...",
        L"\uC0AD\uC81C \uD655\uC778", L"\uD734\uC9C0\uD1B5\uC73C\uB85C \uC774\uB3D9\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?\n\n",
        L"\uC601\uAD6C \uC0AD\uC81C", L"\uC601\uAD6C\uC801\uC73C\uB85C \uC0AD\uC81C\uD558\uC2DC\uACA0\uC2B5\uB2C8\uAE4C?\n\n",
        L"\uACBD\uB85C\uAC00 \uBCF5\uC0AC\uB418\uC5C8\uC2B5\uB2C8\uB2E4.",
        L"\uC5B8\uC5B4 \uC120\uD0DD", L"\uC0AC\uC6A9\uD560 \uC5B8\uC5B4\uB97C \uC120\uD0DD\uD558\uC138\uC694:", L"\uACC4\uC18D"
    },
    // 10: Hindi
    {
        L"hi", L"\u0939\u093F\u0928\u094D\u0926\u0940 (Hindi)", false,
        L"HyperTree - \u0905\u0932\u094D\u091F\u094D\u0930\u093E-\u092B\u093E\u0938\u094D\u091F \u0921\u093F\u0938\u094D\u0915 \u0938\u094D\u092A\u0947\u0938 \u090F\u0928\u093E\u0932\u093E\u0907\u091C\u093C\u0930",
        L"\u092B\u093C\u094B\u0932\u094D\u0921\u0930...", L"\u0938\u094D\u0915\u0948\u0928", L"\u0930\u0926\u094D\u0926", L"\u090F\u0921\u092E\u093F\u0928 (MFT)", L"\u090F\u0932\u093F\u0935\u0947\u091F", L"CSV \u0928\u093F\u0930\u094D\u092F\u093E\u0924",
        L"\u091F\u094D\u0930\u0940\u092E\u0948\u092A", L"\u0938\u093F\u0938\u094D\u091F\u092E \u092B\u093C\u093E\u0907\u0932\u0947\u0902", L"\u0932\u093E\u0907\u091F", L"\u0921\u093E\u0930\u094D\u0915", L"\u092D\u093E\u0937\u093E",
        L"\u0921\u094D\u0930\u093E\u0907\u0935 \u092F\u093E \u092B\u093C\u094B\u0932\u094D\u0921\u0930 \u091A\u0941\u0928\u0947\u0902 \u0914\u0930 \u0938\u094D\u0915\u0948\u0928 \u092A\u0930 \u0915\u094D\u0932\u093F\u0915 \u0915\u0930\u0947\u0902\u0964",
        L"\u091F\u094D\u0930\u0940 \u0935\u094D\u092F\u0942", L"\u092B\u093C\u093E\u0907\u0932 \u0935\u094D\u092F\u0942", L"\u092B\u093C\u093E\u0907\u0932 \u092A\u094D\u0930\u0915\u093E\u0930",
        L"\u0928\u093E\u092E", L"% \u092A\u0948\u0930\u0947\u0902\u091F", L"\u0906\u0915\u093E\u0930", L"\u0906\u0935\u0902\u091F\u093F\u0924", L"\u0938\u0902\u0936\u094B\u0927\u093F\u0924", L"\u092B\u093C\u093E\u0907\u0932\u0947\u0902", L"\u092B\u093C\u094B\u0932\u094D\u0921\u0930",
        L"\u0928\u093E\u092E \u0914\u0930 \u092A\u0925", L"\u090F\u0915\u094D\u0938\u091F\u0947\u0902\u0936\u0928", L"% \u0921\u094D\u0930\u093E\u0907\u0935", L"\u0915\u0941\u0932 \u0906\u0915\u093E\u0930",
        L"\u0928\u093E\u092E, *.ext, -\u0939\u091F\u093E\u090F\u0902, >10mb...", L"\u0938\u093E\u092B\u093C", L"\u0938\u092D\u0940", L"\u090A\u092A\u0930", L"\u0930\u0942\u091F", L"\u091F\u094D\u0930\u0940\u092E\u0948\u092A",
        L"\u092B\u093C\u093F\u0932\u094D\u091F\u0930", L"\u0938\u093F\u0938\u094D\u091F\u092E \u092B\u093C\u093E\u0907\u0932\u0947\u0902 \u0926\u093F\u0916\u093E\u090F\u0902", L"\u091B\u093F\u092A\u0940 \u092B\u093C\u093E\u0907\u0932\u0947\u0902 \u0926\u093F\u0916\u093E\u090F\u0902", L"\u0915\u094D\u0932\u093E\u0909\u0921 \u092B\u093C\u093E\u0907\u0932\u0947\u0902 \u0926\u093F\u0916\u093E\u090F\u0902",
        L"< 1 MB \u091B\u093F\u092A\u093E\u090F\u0902 (\u091F\u094D\u0930\u0940/\u092E\u0948\u092A)", L"\u092A\u0942\u0930\u094D\u0923 \u092A\u0941\u0928\u0903 \u0938\u094D\u0915\u0948\u0928 (\u0915\u0948\u0936 \u091B\u094B\u0921\u093C\u0947\u0902)",
        L"\u0938\u092D\u0940 \u092A\u094D\u0930\u0915\u093E\u0930", L"\u0935\u0940\u0921\u093F\u092F\u094B", L"\u091A\u093F\u0924\u094D\u0930", L"\u0911\u0921\u093F\u092F\u094B", L"\u0906\u0930\u094D\u0915\u093E\u0907\u0935", L"\u0926\u0938\u094D\u0924\u093E\u0935\u0947\u091C\u093C", L"\u090F\u0915\u094D\u091C\u093C\u0940\u0915\u094D\u092F\u0942\u091F\u0947\u092C\u0932", L"\u0915\u094B\u0921", L"\u0915\u0947\u0935\u0932 \u0915\u094D\u0932\u093E\u0909\u0921",
        L"\u0915\u094B\u0908 \u092D\u0940 \u0938\u092E\u092F", L"\u092A\u093F\u091B\u0932\u0947 24 \u0918\u0902\u091F\u0947", L"\u092A\u093F\u091B\u0932\u0947 7 \u0926\u093F\u0928", L"\u092A\u093F\u091B\u0932\u0947 30 \u0926\u093F\u0928", L"1 \u0938\u093E\u0932 \u0938\u0947 \u092A\u0941\u0930\u093E\u0928\u0947",
        L"\u092B\u093C\u093E\u0907\u0932\u0947\u0902", L"[\u0936\u0940\u0930\u094D\u0937 200k]",
        L"\u0916\u094B\u0932\u0947\u0902", L"\u090F\u0915\u094D\u0938\u092A\u094D\u0932\u094B\u0930\u0930 \u092E\u0947\u0902 \u0916\u094B\u0932\u0947\u0902", L"\u092F\u0939\u093E\u0901 \u091C\u093C\u0942\u092E \u0915\u0930\u0947\u0902", L"\u092A\u0925 \u0915\u0949\u092A\u0940 \u0915\u0930\u0947\u0902", L"\u0917\u0941\u0923",
        L"\u0930\u0940\u0938\u093E\u0907\u0915\u0932 \u092C\u093F\u0928 \u092E\u0947\u0902 \u092D\u0947\u091C\u0947\u0902", L"\u0938\u094D\u0925\u093E\u092F\u0940 \u0930\u0942\u092A \u0938\u0947 \u0939\u091F\u093E\u090F\u0902...",
        L"\u092A\u0941\u0937\u094D\u091F\u093F", L"\u0930\u0940\u0938\u093E\u0907\u0915\u0932 \u092C\u093F\u0928 \u092E\u0947\u0902 \u092D\u0947\u091C\u0947\u0902?\n\n",
        L"\u0938\u094D\u0925\u093E\u092F\u0940 \u0939\u091F\u093E\u0928\u093E", L"\u0938\u094D\u0925\u093E\u092F\u0940 \u0930\u0942\u092A \u0938\u0947 \u0939\u091F\u093E\u090F\u0902?\n\n",
        L"\u092A\u0925 \u0915\u0949\u092A\u0940 \u0939\u094B \u0917\u092F\u093E\u0964",
        L"\u092D\u093E\u0937\u093E \u091A\u0941\u0928\u0947\u0902", L"\u0905\u092A\u0928\u0940 \u092A\u0938\u0902\u0926\u0940\u0926\u093E \u092D\u093E\u0937\u093E \u091A\u0941\u0928\u0947\u0902:", L"\u091C\u093E\u0930\u0940 \u0930\u0916\u0947\u0902"
    },
    // 11: Turkish
    {
        L"tr", L"T\u00FCrk\u00E7e", false,
        L"HyperTree - Ultra H\u0131zl\u0131 Disk Alan\u0131 Analizcisi",
        L"Klas\u00F6r...", L"Tara", L"\u0130ptal", L"Y\u00F6netici (MFT)", L"Y\u00FCkselt", L"CSV Aktar",
        L"Harita", L"Sistem Dosyalar\u0131", L"A\u00E7\u0131k", L"Koyu", L"Dil",
        L"Bir s\u00FCr\u00FCc\u00FC veya klas\u00F6r se\u00E7in ve Tara'ya t\u0131klay\u0131n.",
        L"A\u011Fa\u00E7 G\u00F6r\u00FCn\u00FCm\u00FC", L"Dosya G\u00F6r\u00FCn\u00FCm\u00FC", L"Dosya T\u00FCrleri",
        L"Ad", L"% \u00DCst", L"Boyut", L"Ayr\u0131lan", L"De\u011Fi\u015Ftirilme", L"Dosya", L"Klas\u00F6r",
        L"Ad ve Yol", L"Uzant\u0131", L"% S\u00FCr\u00FCc\u00FC", L"Toplam Boyut",
        L"Ad, *.ext, -hari\u00E7, >10mb...", L"Temizle", L"T\u00FCm\u00FC", L"Yukar\u0131", L"K\u00F6k", L"Harita",
        L"Filtreler", L"Sistem Dosyalar\u0131n\u0131 G\u00F6ster", L"Gizli Dosyalar\u0131 G\u00F6ster", L"Bulut Dosyalar\u0131n\u0131 G\u00F6ster",
        L"< 1 MB Gizle (A\u011Fa\u00E7 & Harita)", L"Tam Tarama (\u00D6nbelleksiz)",
        L"T\u00FCm T\u00FCrler", L"Videolar", L"G\u00F6rseller", L"Ses", L"Ar\u015Fivler", L"Belgeler", L"\u00C7al\u0131\u015Ft\u0131r\u0131labilir", L"Kod", L"Yaln\u0131zca Bulut",
        L"Her Zaman", L"Son 24 Saat", L"Son 7 G\u00FCn", L"Son 30 G\u00FCn", L"1 Y\u0131ldan Eski",
        L"dosya", L"[ilk 200b]",
        L"A\u00E7", L"Gezgin'de A\u00E7", L"Buraya Yak\u0131nla\u015Ft\u0131r", L"Tam Yolu Kopyala", L"\u00D6zellikler",
        L"Geri D\u00F6n\u00FC\u015F\u00FCm Kutusuna Ta\u015F\u0131", L"Kal\u0131c\u0131 Olarak Sil...",
        L"Silmeyi Onayla", L"Bu \u00F6\u011Fe Geri D\u00F6n\u00FC\u015F\u00FCm Kutusuna ta\u015F\u0131ns\u0131n m\u0131?\n\n",
        L"Kal\u0131c\u0131 Silme", L"Bu \u00F6\u011Fe KALICI olarak silinsin mi?\n\n",
        L"Yol panoya kopyaland\u0131.",
        L"Dil Se\u00E7in", L"Tercih etti\u011Finiz dili se\u00E7in:", L"Devam"
    }
}};

inline const LangStrings& GetLangStrings(LangId id) {
    size_t idx = static_cast<size_t>(id);
    if (idx >= g_langs.size()) idx = 0;
    return g_langs[idx];
}

inline std::wstring GetConfigPath() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring dir = (len > 0 && len < MAX_PATH) ? std::wstring(appData) + L"\\HyperTree" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

inline bool LoadSavedLanguage(LangId& outLang) {
    std::wstring path = GetConfigPath();
    wchar_t buf[32]{};
    DWORD n = GetPrivateProfileStringW(L"Settings", L"Language", L"", buf, 31, path.c_str());
    if (n == 0) return false;
    for (size_t i = 0; i < g_langs.size(); ++i) {
        if (wcscmp(buf, g_langs[i].code) == 0) {
            outLang = static_cast<LangId>(i);
            return true;
        }
    }
    return false;
}

inline void SaveLanguage(LangId id) {
    std::wstring path = GetConfigPath();
    WritePrivateProfileStringW(L"Settings", L"Language", GetLangStrings(id).code, path.c_str());
}
