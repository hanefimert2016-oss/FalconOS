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
 *  Because there is no disk yet, "install" only flips a bit; the prg.c
 *  layer is the contract that real persistent install routines will fulfil
 *  once we have a filesystem.  This is exactly the architecture macOS used
 *  for `installer(8)` before the AppStore: package metadata + state file.
 * ============================================================================= */
#include "falcon.h"

/* Compile-time catalog -----------------------------------------------------*/
static const prg_pkg_t CATALOG[] = {
    /* Built-in OS packages — installed = always true, can't remove ----- */
    { "falcon-kernel",  "1.0.0", "Long-mode microkernel + IDT/PIC/PIT",      "core",     "",                  140, true  },
    { "falcon-shell",   "1.0.0", "Lumen + Nox + Liquid + Nordic + Rose desktop shell", "core", "falcon-kernel", 110, true  },
    { "falcon-apps",    "1.0.0", "12 stock apps + Launchpad + window manager", "core", "falcon-shell",         86, true  },
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
};

#define N_CATALOG ((i32)(sizeof(CATALOG) / sizeof(CATALOG[0])))

/* runtime install bitmap — index i ↔ CATALOG[i] ------------------------*/
static u8 g_installed[N_CATALOG];

static bool g_inited = false;
static void init_once(void)
{
    if (g_inited) return;
    for (i32 i = 0; i < N_CATALOG; i++) {
        g_installed[i] = CATALOG[i].builtin ? 1 : 0;
    }
    g_inited = true;
}

i32 prg_count(void) { init_once(); return N_CATALOG; }

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
    return g_installed[i] != 0;
}

bool prg_install(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return false;
    g_installed[i] = 1;
    return true;
}

bool prg_remove(i32 i)
{
    init_once();
    if (i < 0 || i >= N_CATALOG) return false;
    if (CATALOG[i].builtin)      return false;
    g_installed[i] = 0;
    return true;
}

i32 prg_installed_count(void)
{
    init_once();
    i32 n = 0;
    for (i32 i = 0; i < N_CATALOG; i++) if (g_installed[i]) n++;
    return n;
}
