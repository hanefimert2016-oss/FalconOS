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
    { "falcon-kernel",  "5.0.0", "Long-mode microkernel + IDT/PIC/PIT",      "core",     "",                  120, true  },
    { "falcon-shell",   "5.0.0", "Lumen + Nox desktop shell",                "core",     "falcon-kernel",      96, true  },
    { "falcon-apps",    "5.0.0", "12 stock apps + Launchpad",                "core",     "falcon-shell",       72, true  },
    { "linux-uapi",     "6.6.0", "Linux UAPI compatibility headers",         "compat",   "falcon-kernel",      14, true  },
    { "ata-pio",        "1.2.0", "Linux libata-style ATA PIO driver",        "drivers",  "linux-uapi",         18, true  },
    { "hid-keymap",     "1.0.0", "Linux PS/2 + HID scancode tables",         "drivers",  "linux-uapi",         11, true  },

    /* User-installable extras (start uninstalled) ---------------------- */
    { "theme-nordic",   "1.4.2", "Nordic-inspired blue/grey theme bundle",   "themes",   "falcon-shell",        9, false },
    { "theme-rosegold", "1.1.0", "Pink + warm-gold accent theme",            "themes",   "falcon-shell",        7, false },
    { "app-paint",      "0.6.0", "Lightweight pixel paint (mouse-driven)",   "apps",     "falcon-apps",        24, false },
    { "app-music",      "0.4.1", "Synth pad + arpeggiator (PC speaker)",     "apps",     "falcon-apps",        18, false },
    { "app-snake",      "1.0.0", "Classic snake; arrows + Esc",              "games",    "falcon-apps",         6, false },
    { "app-dosbox-lite","0.2.0", "Tiny x86 real-mode interpreter (sandbox)", "compat",   "falcon-apps",        86, false },
    { "lib-png",        "1.6.39","PNG decoder, derived from libpng",         "libraries","falcon-kernel",      54, false },
    { "lib-zlib",       "1.3.0", "zlib compression — drop-in libz.so",       "libraries","falcon-kernel",      48, false },
    { "vim-tiny",       "9.0.0", "Vim text editor, embedded subset",         "apps",     "falcon-apps",       128, false },
    { "busybox-static", "1.36.1","Statically linked busybox (planned ELF)",  "compat",   "linux-uapi",        780, false },
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
