/* =============================================================================
 *  FalconOS — multi-user database  (v5+)
 * -----------------------------------------------------------------------------
 *  Up to FALCON_MAX_USERS local accounts.  Lives entirely inside SET.users[]
 *  so that diskdb_save() persists everything in one shot to LBA0.  Passwords
 *  are stored as 32-byte PBKDF2-HMAC-SHA256 hashes with a 16-byte per-user
 *  random salt (see kernel/auth.c).
 *
 *  The first user created in the installer is marked as is_default=true and
 *  becomes SET.default_user.  Every cold boot opens the lock screen focused
 *  on that user; ←/→ in the lock screen lets you swap to any other account.
 * ============================================================================= */
#include "falcon.h"

/* Stretching factor for the password KDF. 100 000 rounds of HMAC-SHA256
 * keep verification under ~500 ms on a stock 2 GHz core (still snappy
 * for users) but double the cost of an offline brute-force attack
 * vs. v5.2.  This matches modern OWASP guidance for SHA-256 PBKDF2.   */
#define PBKDF2_ROUNDS  100000

static i32 first_free_slot(void)
{
    for (i32 i = 0; i < FALCON_MAX_USERS; i++)
        if (!SET.users[i].in_use) return i;
    return -1;
}

static void hash_password(falcon_user_t *u, const char *plain)
{
    if (plain == NULL || plain[0] == 0) {
        u->no_password = 1;
        k_memset(u->hash, 0, FALCON_HASH_BYTES);
        rng_bytes(u->salt, FALCON_SALT_BYTES);   /* still randomise salt */
        return;
    }
    u->no_password = 0;
    rng_bytes(u->salt, FALCON_SALT_BYTES);
    pbkdf2_sha256((const u8 *)plain, (u32)k_strlen(plain),
                  u->salt, FALCON_SALT_BYTES,
                  PBKDF2_ROUNDS,
                  u->hash, FALCON_HASH_BYTES);
}

i32 users_add(const char *name, const char *plaintext_pwd, accent_t accent)
{
    i32 slot = first_free_slot();
    if (slot < 0) return -1;

    falcon_user_t *u = &SET.users[slot];
    k_memset(u, 0, sizeof(*u));
    u->in_use = true;
    u->accent = accent;

    /* Copy name (truncate safely) */
    i32 i = 0;
    while (name[i] && i < FALCON_NAME_BYTES - 1) { u->name[i] = name[i]; i++; }
    u->name[i] = 0;
    if (i == 0) k_strcpy(u->name, "User");

    hash_password(u, plaintext_pwd);

    /* First user becomes default */
    if (SET.user_count == 0) {
        u->is_default     = true;
        SET.default_user  = slot;
    }
    SET.user_count++;
    return slot;
}

bool users_remove(i32 idx)
{
    if (idx < 0 || idx >= FALCON_MAX_USERS) return false;
    if (!SET.users[idx].in_use) return false;
    if (SET.user_count <= 1) return false;          /* never remove last  */

    bool was_default = SET.users[idx].is_default;
    k_memset(&SET.users[idx], 0, sizeof(falcon_user_t));
    SET.user_count--;

    if (was_default) {
        /* promote the next live slot */
        for (i32 i = 0; i < FALCON_MAX_USERS; i++) {
            if (SET.users[i].in_use) {
                SET.users[i].is_default = true;
                SET.default_user = i;
                break;
            }
        }
    }
    if (SET.active_user == idx) SET.active_user = SET.default_user;
    return true;
}

bool users_set_default(i32 idx)
{
    if (idx < 0 || idx >= FALCON_MAX_USERS) return false;
    if (!SET.users[idx].in_use) return false;
    for (i32 i = 0; i < FALCON_MAX_USERS; i++)
        SET.users[i].is_default = false;
    SET.users[idx].is_default = true;
    SET.default_user = idx;
    return true;
}

bool users_change_password(i32 idx, const char *new_plaintext)
{
    if (idx < 0 || idx >= FALCON_MAX_USERS) return false;
    if (!SET.users[idx].in_use) return false;
    hash_password(&SET.users[idx], new_plaintext);
    return true;
}

bool users_verify(i32 idx, const char *plaintext)
{
    if (idx < 0 || idx >= FALCON_MAX_USERS) return false;
    falcon_user_t *u = &SET.users[idx];
    if (!u->in_use) return false;
    if (u->no_password) {
        bool ok = (plaintext == NULL || plaintext[0] == 0);
        if (ok) {
            u->failed_attempts = 0;
            u->last_login_uptime_ms = pit_ms();
        }
        return ok;
    }

    if (plaintext == NULL || plaintext[0] == 0) {
        u->failed_attempts++;
        u->last_fail_uptime_ms = pit_ms();
        return false;
    }

    u8 candidate[FALCON_HASH_BYTES];
    pbkdf2_sha256((const u8 *)plaintext, (u32)k_strlen(plaintext),
                  u->salt, FALCON_SALT_BYTES,
                  PBKDF2_ROUNDS,
                  candidate, FALCON_HASH_BYTES);
    /* constant-time compare so the verifier can't be timing-attacked */
    u8 diff = 0;
    for (i32 i = 0; i < FALCON_HASH_BYTES; i++)
        diff |= (u8)(candidate[i] ^ u->hash[i]);

    /* Zero the candidate hash so an attacker who later reads stack
     * memory can't recover this guess (and, by extension, narrow the
     * search space).                                                  */
    k_explicit_bzero(candidate, sizeof candidate);

    bool ok = (diff == 0);
    if (ok) {
        u->failed_attempts        = 0;
        u->last_login_uptime_ms   = pit_ms();
    } else {
        u->failed_attempts++;
        u->last_fail_uptime_ms    = pit_ms();
    }
    return ok;
}

i32 password_strength(const char *plain)
{
    if (plain == NULL) return 0;
    i32 len = (i32)k_strlen(plain);
    if (len == 0) return 0;
    if (len < 4)  return 0;

    i32 has_lower = 0, has_upper = 0, has_digit = 0, has_other = 0;
    for (i32 i = 0; i < len; i++) {
        char c = plain[i];
        if      (c >= 'a' && c <= 'z') has_lower = 1;
        else if (c >= 'A' && c <= 'Z') has_upper = 1;
        else if (c >= '0' && c <= '9') has_digit = 1;
        else                            has_other = 1;
    }
    i32 classes = has_lower + has_upper + has_digit + has_other;

    if (len < 6)                  return 1;
    if (len >= 12 && classes >= 3) return 3;
    if (len >= 8  && classes >= 2) return 2;
    return 1;
}

const falcon_user_t *users_at(i32 idx)
{
    if (idx < 0 || idx >= FALCON_MAX_USERS) return NULL;
    if (!SET.users[idx].in_use) return NULL;
    return &SET.users[idx];
}

i32 users_count(void) { return SET.user_count; }
