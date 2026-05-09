/* =============================================================================
 *  FalconOS — `prg` package manager  (v5)
 * -----------------------------------------------------------------------------
 *  `prg` ("paket-yoneticisi" — "Falcon paket runner") is the unified package
 *  manager for FalconOS.  Its CLI deliberately mirrors Linux package managers
 *  (apt / pacman / dnf) so that future Linux-package compatibility shims can
 *  drop in seamlessly:
 *
 *      prg list                 # all known packages
 *      prg search <term>        # substring filter on name + summary
 *      prg info <pkg>           # show one package
 *      prg install <pkg>        # mark installed (in-memory db)
 *      prg remove <pkg>         # remove (built-ins refuse)
 *      prg installed            # list installed only
 *
 *  Built-in OS packages are always-installed and read-only.  User-installable
 *  packages (theme bundles, extra apps) live in a fixed compile-time catalog
 *  and toggle their `installed` flag when the user installs/removes them.
 *
 *  Because there was no multi-file FS, early "install" only flipped a bit;
 *  FalconOS now also drops a `/`-visible receipt into the Terminal ramdisk
 *  (filename r<slot>) whenever a non-built-in catalogue entry installs, and
 *  diskdb persists the SET.prg_installed[] bitmap alongside user settings.
 * ============================================================================= */
#include "falcon.h"

/* Compile-time catalog -----------------------------------------------------*/
static const prg_pkg_t CATALOG[] = {
    /* Built-in OS packages — installed = always true, can't remove ----- */
    { "falcon-kernel",  "1.0.0", "Long-mode microkernel + IDT/PIC/PIT",      "core",     "",                  140, true  },
    { "falcon-shell",   "1.0.0", "Lumen + Nox + Liquid + Nordic + Rose desktop shell", "core", "falcon-kernel", 110, true  },
    { "falcon-apps",    "1.0.0", "12 stock apps + Launchpad + window manager", "core", "falcon-shell",         86, true  },
    { "app-falco-browser","1.0.0","Falco browser/search shell (indexed + API-ready)", "apps", "falcon-apps",    28, true  },
    { "falcon-shell-sh","1.0.0", "POSIX-subset shell (cd/ls/cat/echo/if/for)", "core",  "falcon-apps",          22, true  },
    { "falcon-aero",    "1.0.0", "Aero / Liquid Glass blur compositor",       "core",   "falcon-shell",         18, true  },
    { "falcon-installer","1.0.0","8-step installer + 5 languages + USB image","core",   "falcon-shell",         28, true  },
    { "falcon-auth",    "1.0.0", "PBKDF2-HMAC-SHA256 100k + per-user salts",  "security","falcon-kernel",       16, true  },
    { "falcon-diskdb",  "1.0.0", "Persistent settings/users superblock",      "core",    "falcon-kernel",       12, true  },
    { "linux-uapi",     "6.6.0", "Linux UAPI compatibility headers",          "compat",  "falcon-kernel",       14, true  },
    { "ata-pio",        "1.2.0", "ATA PIO disk driver (libata-style)",        "drivers", "linux-uapi",          18, true  },
    { "hid-keymap",     "1.0.0", "PS/2 + HID scancode tables (TR/EN/DE/FR/ES)","drivers","linux-uapi",          11, true  },

    /* Themes ----------------------------------------------------------- */
    { "theme-lumen",     "1.0.0","Lumen — bright off-white macOS-Big-Sur",     "themes",  "falcon-shell",        4, true  },
    { "theme-nox",       "1.0.0","Nox — dark counterpart to Lumen",            "themes",  "falcon-shell",        4, true  },
    { "theme-liquid",    "1.0.0","Liquid Glass — frosted-aqua, max blur",      "themes",  "falcon-aero",         5, true  },
    { "theme-nordic",    "1.4.2","Nordic — cool blue-grey, low contrast",      "themes",  "falcon-shell",        9, true  },
    { "theme-rosegold",  "1.1.0","Rose Gold — pink + warm gold accent",        "themes",  "falcon-shell",        7, true  },

    /* User-installable extras (start uninstalled) ---------------------- */
    { "app-paint",       "0.6.0","Lightweight pixel paint (mouse-driven)",    "apps",    "falcon-apps",        24, false },
    { "app-music",       "0.4.1","Synth pad + arpeggiator (PC speaker)",      "apps",    "falcon-apps",        18, false },
    { "app-snake",       "1.0.0","Classic snake; arrows + Esc",                "games",   "falcon-apps",         6, false },
    { "app-2048",        "1.0.0","2048 — slide tiles, merge powers of two",   "games",   "falcon-apps",         5, false },
    { "app-tetris",      "1.0.0","Tetris-clone with rotation + soft drop",    "games",   "falcon-apps",         8, false },
    { "app-mines",       "1.0.0","Minesweeper, three difficulty presets",     "games",   "falcon-apps",         5, false },
    { "app-chess",       "1.0.0","Chess board + move generator (no engine)",  "games",   "falcon-apps",        14, false },
    { "app-markdown",    "1.0.0","Markdown reader (headings, lists, code)",   "apps",    "falcon-apps",        16, false },
    { "app-hex-editor",  "1.0.0","16-byte hex/ASCII editor for shfs",         "apps",    "falcon-shell-sh",    14, false },
    { "app-monitor",     "1.0.0","Live IRQ + IO + memory monitor",            "apps",    "falcon-apps",         9, false },
    { "app-network-cfg", "0.3.0","Network configuration (planned virtio-net)","apps",    "falcon-apps",        10, false },
    { "app-screensaver", "1.0.0","Aero starfield + clock screensaver",        "apps",    "falcon-aero",         7, false },
    { "app-dosbox-lite", "0.2.0","Tiny x86 real-mode interpreter (sandbox)",  "compat",  "falcon-apps",        86, false },
    { "app-google-chrome","1.0.0","Chrome compatibility launcher bridge",      "compat",  "linux-uapi",         64, false },
    { "app-heroic-launcher","1.0.0","Heroic Games Launcher compatibility shim", "compat",  "linux-uapi",         52, false },
    { "lib-png",         "1.6.39","PNG decoder, derived from libpng",         "libraries","falcon-kernel",     54, false },
    { "lib-zlib",        "1.3.0","zlib compression — drop-in libz.so",        "libraries","falcon-kernel",     48, false },
    { "lib-jpeg",        "9.5.0","libjpeg-turbo SIMD decoder (planned)",      "libraries","falcon-kernel",     86, false },
    { "lib-freetype",    "2.13.0","FreeType anti-aliased font rasteriser",    "libraries","falcon-shell",     142, false },
    { "vim-tiny",        "9.0.0","Vim text editor, embedded subset",          "apps",    "falcon-apps",       128, false },
    { "nano-tiny",       "7.2",  "GNU nano subset for shfs",                  "apps",    "falcon-shell-sh",    36, false },
    { "busybox-static",  "1.36.1","Statically linked busybox (planned ELF)",  "compat",  "linux-uapi",        780, false },
    { "coreutils-min",   "9.4",  "Minimal coreutils (ls/cat/cp/mv/rm) shim",  "compat",  "falcon-shell-sh",    42, false },
    { "lang-pack-extra", "1.0.0","Italian + Portuguese + Polish (planned)",   "i18n",    "falcon-shell",        8, false },
    { "icon-pack-classic","1.0.0","Classic Lumen icon set (rounded)",         "themes",  "falcon-shell",        4, false },
    { "icon-pack-flat",  "1.0.0","Flat / minimal icon variant",               "themes",  "falcon-shell",        4, false },
    { "wallpaper-pack",  "1.0.0","12 framebuffer wallpapers (gradients)",     "themes",  "falcon-shell",       18, false },
    { "fonts-mono-pack", "1.0.0","Inter / Mono / Display bitmap font pack",   "themes",  "falcon-shell",       12, false },

    /* ---- catalogue expansion (FalconOS 1.1) ----------------------------- */
    /* extra games */
    { "app-tictactoe",   "1.0.0","Tic-tac-toe vs. naive AI",                  "games",   "falcon-apps",         3, false },
    { "app-pong",        "1.0.0","Two-paddle Pong, keyboard duel",            "games",   "falcon-apps",         5, false },
    { "app-breakout",    "1.0.0","Brick-breaker — 12 levels",                 "games",   "falcon-apps",         9, false },
    { "app-solitaire",   "1.0.0","Klondike solitaire, 1-card draw",           "games",   "falcon-apps",        18, false },
    { "app-sudoku",      "1.0.0","Sudoku, four difficulty presets",           "games",   "falcon-apps",        12, false },
    { "app-snake-classic","1.2.0","Snake remastered, growing dot trail",      "games",   "falcon-apps",         6, false },

    /* extra apps */
    { "app-stopwatch",   "1.0.0","Lap stopwatch, sub-second readout",         "apps",    "falcon-apps",         3, false },
    { "app-timer",       "1.0.0","Countdown timer with PC-speaker alarm",     "apps",    "falcon-apps",         4, false },
    { "app-todo",        "1.0.0","Tiny todo list, persists in shfs",          "apps",    "falcon-shell-sh",     5, false },
    { "app-weather-pro", "0.9.0","7-day forecast widget for the dashboard",   "apps",    "falcon-apps",        14, false },
    { "app-translate",   "0.4.0","Phrase translator (TR/EN/DE/FR/ES dataset)","apps",    "falcon-apps",        22, false },
    { "app-currency",    "1.0.0","Static FX-rate calculator, 18 currencies",  "apps",    "falcon-apps",         6, false },
    { "app-piano",       "1.0.0","On-screen 2-octave piano (PC speaker)",     "apps",    "falcon-apps",        12, false },
    { "app-disk-usage",  "1.0.0","Visual du -h on the FalconFS superblock",   "apps",    "falcon-apps",         5, false },

    /* themes */
    { "theme-monokai",   "1.0.0","Monokai high-contrast (orange / pink)",     "themes",  "falcon-shell",        4, false },
    { "theme-solarized-dark","1.0.0","Solarized Dark (Ethan Schoonover)",     "themes",  "falcon-shell",        4, false },
    { "theme-solarized-light","1.0.0","Solarized Light, low eye-strain",      "themes",  "falcon-shell",        4, false },
    { "theme-cyberpunk", "1.0.0","Neon magenta / cyan, retro-futurist",       "themes",  "falcon-shell",        5, false },
    { "theme-dracula",   "1.0.0","Dracula community theme, dark purple",      "themes",  "falcon-shell",        4, false },
    { "theme-forest",    "1.0.0","Forest green, warm beige accents",          "themes",  "falcon-shell",        4, false },

    /* fonts + icons */
    { "font-inter",      "4.0",  "Inter UI font (8x16 + 16x32 bitmap render)","themes",  "falcon-shell",        9, false },
    { "font-jetbrains",  "2.304","JetBrains Mono — coding terminal font",     "themes",  "falcon-shell",       11, false },
    { "font-fira-code",  "6.2",  "Fira Code w/ ligatures (display only)",     "themes",  "falcon-shell",       12, false },
    { "icon-pack-papirus","1.0.0","Papirus-style flat icon set",              "themes",  "falcon-shell",        7, false },
    { "icon-pack-monochrome","1.0.0","Monochrome glyph icon set (mono UI)",   "themes",  "falcon-shell",        5, false },

    /* dev / tooling */
    { "dev-tools",       "1.0.0","gcc-style build glue + objdump-lite",       "dev",     "falcon-shell-sh",    44, false },
    { "git-tiny",        "2.42.0","git-clone-from-disk-only stub",            "dev",     "falcon-shell-sh",    18, false },
    { "make-tiny",       "4.4",  "GNU make subset for falcon-shell-sh",       "dev",     "falcon-shell-sh",    14, false },
    { "ssh-tiny",        "9.6p1","ssh client (planned virtio-net loopback)",  "dev",     "falcon-shell-sh",    52, false },
    { "python-mini",     "3.12.0","Python REPL subset, no stdlib",            "dev",     "falcon-apps",        96, false },
    { "lua-mini",        "5.4.6","Lua interpreter, sandboxed",                "dev",     "falcon-apps",        24, false },

    /* network / system tools */
    { "network-tools",   "1.0.0","ip / ping / arp / traceroute (planned net)","tools",   "linux-uapi",         18, false },
    { "system-monitor",  "1.0.0","htop-style process + IRQ monitor",          "tools",   "falcon-apps",         8, false },
    { "diskutil-pro",    "1.0.0","fdisk / parted-style partition editor",     "tools",   "ata-pio",            22, false },

    /* language packs */
    { "lang-pack-de",    "1.0.0","Full German UI strings",                    "i18n",    "falcon-shell",        6, false },
    { "lang-pack-fr",    "1.0.0","Full French UI strings",                    "i18n",    "falcon-shell",        6, false },
    { "lang-pack-es",    "1.0.0","Full Spanish UI strings",                   "i18n",    "falcon-shell",        6, false },
    { "lang-pack-jp",    "1.0.0","Japanese UI (planned hiragana atlas)",      "i18n",    "falcon-shell",       18, false },

    /* libraries (planned ELF link) */
    { "lib-openssl",     "3.2.0","OpenSSL crypto + TLS (planned)",            "libraries","falcon-kernel",     124, false },
    { "lib-sqlite",      "3.45.0","SQLite single-file db (planned)",          "libraries","falcon-kernel",      62, false },
    { "lib-cairo",       "1.18.0","Cairo 2D vector graphics (planned)",       "libraries","falcon-shell",      138, false },

    /* compat shims */
    { "wine-tiny",       "9.0",  "Wine subset for win32 console apps (plan)", "compat",  "linux-uapi",        420, false },
    { "qemu-user",       "8.2.0","qemu-user-static pieces (planned)",         "compat",  "linux-uapi",        260, false },
};

#define N_CATALOG ((i32)(sizeof(CATALOG) / sizeof(CATALOG[0])))

/* The on-disk install bitmap lives in SET.prg_installed[] so that an
 * `install vim-tiny` survives a cold reboot.  init_once() folds three
 * sources together on first access:
 *
 *   1. Built-ins — always 1 regardless of what disk says.  Removing the
 *      kernel by editing a sector should not be possible.
 *   2. Disk state — whatever diskdb_load() pulled out of LBA0..3 (this
 *      is already in SET by the time settings_init() returns).
 *   3. New catalogue entries — slots beyond what an older disk knew about
 *      default to 0 (uninstalled), preserving forward compatibility when
 *      the catalogue grows between OS releases.
 *
 * After the merge SET.prg_installed[] is the single source of truth and
 * every install/remove syncs the new value back to disk via diskdb_save(). */
static bool g_inited = false;
static void init_once(void)
{
    if (g_inited) return;
    for (i32 i = 0; i < N_CATALOG; i++) {
        if (CATALOG[i].builtin) {
            SET.prg_installed[i] = 1;
        }
        /* non-built-ins keep whatever value diskdb_load() restored
         * (0 by default if no disk / fresh install).               */
    }
    g_inited = true;
}

i32 prg_count(void) { init_once(); return N_CATALOG; }

static i32 catalog_idx_by_name(const char *name)
{
    if (!name || !name[0]) return -1;
    for (i32 i = 0; i < N_CATALOG; i++) {
        if (k_strcmp(CATALOG[i].name, name) == 0) return i;
    }
    return -1;
}

const prg_pkg_t *prg_at(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return NULL;
    return &CATALOG[i];
}

const prg_pkg_t *prg_find(const char *name)
{
    init_once();
    for (i32 i = 0; i < N_CATALOG; i++) {
        if (k_strcmp(CATALOG[i].name, name) == 0) return &CATALOG[i];
    }
    return NULL;
}

bool prg_is_installed(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return false;
    return SET.prg_installed[i] != 0;
}

bool prg_install(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return false;
    if (SET.prg_installed[i]) return true;          /* already installed */

    /* Resolve a single depends-edge recursively (catalog uses one direct
     * dependency string per package). Depth guard prevents accidental cycles
     * from corrupt future catalog edits.                                     */
    i32 chain[128];
    i32 nchain = 0;
    i32 cur = i;
    while (cur >= 0 && !SET.prg_installed[cur]) {
        if (nchain >= (i32)(sizeof(chain) / sizeof(chain[0]))) return false;
        for (i32 k = 0; k < nchain; k++) if (chain[k] == cur) return false;
        chain[nchain++] = cur;

        const char *dep = CATALOG[cur].depends;
        if (!dep || !dep[0]) break;
        cur = catalog_idx_by_name(dep);
        if (cur < 0) return false;
    }

    /* Install deepest dependency first, then the requested package. */
    for (i32 k = nchain - 1; k >= 0; k--) {
        SET.prg_installed[chain[k]] = 1;
        apps_pkg_on_install(chain[k]);
    }
    diskdb_save();
    return true;
}

bool prg_remove(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return false;
    if (CATALOG[i].builtin)      return false;
    if (!SET.prg_installed[i]) return true;

    /* Prevent removing a package that another installed package depends on. */
    for (i32 j = 0; j < N_CATALOG; j++) {
        if (j == i || !SET.prg_installed[j]) continue;
        const char *dep = CATALOG[j].depends;
        if (dep && dep[0] && k_strcmp(dep, CATALOG[i].name) == 0) {
            return false;
        }
    }

    SET.prg_installed[i] = 0;
    apps_pkg_on_remove(i);
    diskdb_save();
    return true;
}

i32 prg_installed_count(void)
{
    init_once();
    i32 n = 0;
    for (i32 i = 0; i < N_CATALOG; i++) if (SET.prg_installed[i]) n++;
    return n;
}
