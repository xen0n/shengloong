#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <elf.h>

#include "buildconfig.gen.h"
#include "cfg.h"
#include "ctx.h"
#include "gettext.h"
#include "processing_symbol_versions.h"

#define _(x) gettext(x)

// Structure to track unique library and version combinations
struct version_info {
    char *library;
    char *version;
    struct version_info *next;
};

static struct version_info *g_version_list = NULL;

/////////////////////////////////////////////////////////////////////////////

// Check if a version is already in the list
static bool version_exists(const char *library, const char *version)
{
    struct version_info *entry = g_version_list;
    while (entry != NULL) {
        if (strcmp(entry->library, library) == 0 && 
            strcmp(entry->version, version) == 0) {
            return true;
        }
        entry = entry->next;
    }
    return false;
}

// Add a new version to the list
static void add_version(const char *library, const char *version)
{
    if (version_exists(library, version)) {
        return;
    }
    
    struct version_info *new_entry = malloc(sizeof(struct version_info));
    if (new_entry == NULL) {
        return;
    }
    
    new_entry->library = strdup(library);
    new_entry->version = strdup(version);
    
    // Check if strdup failed
    if (new_entry->library == NULL || new_entry->version == NULL) {
        free(new_entry->library);
        free(new_entry->version);
        free(new_entry);
        return;
    }
    
    new_entry->next = g_version_list;
    g_version_list = new_entry;
}

void collect_symbol_versions(struct sl_elf_ctx *ctx, Elf_Scn *scn, size_t n)
{
    size_t i = 0;
    Elf_Data *d = NULL;
    
    // Note: This uses Elf64_Verneed types, which is safe because process_elf()
    // already checks for ELF64 before calling this function. All LoongArch
    // systems use ELF64.
    
    while (i < n && (d = elf_getdata(scn, d)) != NULL) {
        Elf64_Verneed *vn = (Elf64_Verneed *)(d->d_buf);
        while (i < n) {
            // Get the library name
            const char *lib_name = sl_elf_dynstr(ctx, vn->vn_file);
            
            // Iterate through auxiliary entries (versions)
            Elf64_Vernaux *aux = (Elf64_Vernaux *)((uint8_t *)vn + vn->vn_aux);
            for (size_t j = 0; j < vn->vn_cnt; j++) {
                const char *ver_name = sl_elf_raw_dynstr(ctx, aux->vna_name);
                add_version(lib_name, ver_name);
                
                if (aux->vna_next == 0) {
                    break;
                }
                aux = (Elf64_Vernaux *)((uint8_t *)aux + aux->vna_next);
            }
            
            i++;
            if (vn->vn_next == 0) {
                break;
            }
            vn = (Elf64_Verneed *)((uint8_t *)vn + vn->vn_next);
        }
    }
}

// Comparison function for sorting versions
static int compare_versions(const void *a, const void *b)
{
    const struct version_info *va = *(const struct version_info **)a;
    const struct version_info *vb = *(const struct version_info **)b;
    
    int lib_cmp = strcmp(va->library, vb->library);
    if (lib_cmp != 0) {
        return lib_cmp;
    }
    
    return strcmp(va->version, vb->version);
}

void symbol_versions_print_final_report(void)
{
    if (g_version_list == NULL) {
        printf(_(
            "\n\x1b[32m * \x1b[mNo symbol version dependencies found.\n\n"
        ));
        return;
    }
    
    printf(_("\n\x1b[1;33m# Symbol Version Dependencies\x1b[m\n\n"));
    
    // Count total entries and build array for sorting
    int count = 0;
    struct version_info *entry = g_version_list;
    while (entry != NULL) {
        count++;
        entry = entry->next;
    }
    
    // Create array and sort
    struct version_info **sorted = malloc(count * sizeof(struct version_info *));
    if (sorted == NULL) {
        return;
    }
    
    entry = g_version_list;
    for (int i = 0; i < count; i++) {
        sorted[i] = entry;
        entry = entry->next;
    }
    
    qsort(sorted, count, sizeof(struct version_info *), compare_versions);
    
    // Print sorted versions grouped by library
    const char *current_lib = NULL;
    for (int i = 0; i < count; i++) {
        if (current_lib == NULL || strcmp(current_lib, sorted[i]->library) != 0) {
            if (current_lib != NULL) {
                printf("\n");
            }
            printf(_("  \x1b[36m%s\x1b[m:\n"), sorted[i]->library);
            current_lib = sorted[i]->library;
        }
        printf("    %s\n", sorted[i]->version);
    }
    
    printf(_(
        "\n"
        "\x1b[32m * \x1b[mThese are the symbol version baselines required by binaries\n"
        "   on your system. Ensure your shared libraries provide these versions.\n"
        "\n"
    ));
    
    free(sorted);
    
    // Cleanup
    while (g_version_list != NULL) {
        struct version_info *next = g_version_list->next;
        free(g_version_list->library);
        free(g_version_list->version);
        free(g_version_list);
        g_version_list = next;
    }
}
